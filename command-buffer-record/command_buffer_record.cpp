/*
 * Copyright (c) 2024 The Khronos Group Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * OpenCL is a trademark of Apple Inc. used under license by Khronos.
 */

#include "command_buffer_record.hpp"

#include <algorithm>
#include <cstring>

struct _cl_icd_dispatch dispatch;
const struct _cl_icd_dispatch *tdispatch;

namespace {

std::mutex &mutex() {
  static std::mutex m;
  return m;
}

std::map<cl_command_queue, std::shared_ptr<recording_session>> &sessions() {
  static std::map<cl_command_queue, std::shared_ptr<recording_session>> s;
  return s;
}

std::map<cl_command_queue, cl_command_buffer_khr> &finalized() {
  static std::map<cl_command_queue, cl_command_buffer_khr> f;
  return f;
}

std::map<cl_platform_id, command_buffer_fns> &platform_fns() {
  static std::map<cl_platform_id, command_buffer_fns> p;
  return p;
}

cl_platform_id queue_platform(cl_command_queue queue) {
  cl_device_id device = nullptr;
  if (tdispatch->clGetCommandQueueInfo(queue, CL_QUEUE_DEVICE, sizeof(device),
                                       &device, nullptr) != CL_SUCCESS)
    return nullptr;
  cl_platform_id platform = nullptr;
  if (tdispatch->clGetDeviceInfo(device, CL_DEVICE_PLATFORM, sizeof(platform),
                                 &platform, nullptr) != CL_SUCCESS)
    return nullptr;
  return platform;
}

cl_device_id queue_device(cl_command_queue queue) {
  cl_device_id device = nullptr;
  if (tdispatch->clGetCommandQueueInfo(queue, CL_QUEUE_DEVICE, sizeof(device),
                                       &device, nullptr) != CL_SUCCESS)
    return nullptr;
  return device;
}

cl_context queue_context(cl_command_queue queue) {
  cl_context context = nullptr;
  if (tdispatch->clGetCommandQueueInfo(queue, CL_QUEUE_CONTEXT, sizeof(context),
                                       &context, nullptr) != CL_SUCCESS)
    return nullptr;
  return context;
}

bool queue_is_out_of_order(cl_command_queue queue) {
  cl_command_queue_properties properties = 0;
  if (tdispatch->clGetCommandQueueInfo(queue, CL_QUEUE_PROPERTIES,
                                       sizeof(properties), &properties,
                                       nullptr) != CL_SUCCESS)
    return false;
  return (properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE) != 0;
}

template <typename Fn>
Fn resolve(cl_platform_id platform, const char *name) {
  return reinterpret_cast<Fn>(
      tdispatch->clGetExtensionFunctionAddressForPlatform(platform, name));
}

// Resolves, once per platform, the cl_khr_command_buffer entry points the
// layer needs. Returns nullptr if the platform does not support the extension,
// in which case the layer stays a pure pass-through.
const command_buffer_fns *get_fns(cl_platform_id platform) {
  if (!platform)
    return nullptr;
  auto it = platform_fns().find(platform);
  if (it != platform_fns().end())
    return it->second.create ? &it->second : nullptr;

  command_buffer_fns fns = {};
  fns.create = resolve<clCreateCommandBufferKHR_fn>(
      platform, "clCreateCommandBufferKHR");
  fns.finalize = resolve<clFinalizeCommandBufferKHR_fn>(
      platform, "clFinalizeCommandBufferKHR");
  fns.retain = resolve<clRetainCommandBufferKHR_fn>(
      platform, "clRetainCommandBufferKHR");
  fns.release = resolve<clReleaseCommandBufferKHR_fn>(
      platform, "clReleaseCommandBufferKHR");
  fns.barrier = resolve<clCommandBarrierWithWaitListKHR_fn>(
      platform, "clCommandBarrierWithWaitListKHR");
  fns.ndrange = resolve<clCommandNDRangeKernelKHR_fn>(
      platform, "clCommandNDRangeKernelKHR");
  fns.copy_buffer = resolve<clCommandCopyBufferKHR_fn>(
      platform, "clCommandCopyBufferKHR");
  fns.copy_buffer_rect = resolve<clCommandCopyBufferRectKHR_fn>(
      platform, "clCommandCopyBufferRectKHR");
  fns.fill_buffer = resolve<clCommandFillBufferKHR_fn>(
      platform, "clCommandFillBufferKHR");
  fns.copy_image = resolve<clCommandCopyImageKHR_fn>(
      platform, "clCommandCopyImageKHR");
  fns.copy_image_to_buffer = resolve<clCommandCopyImageToBufferKHR_fn>(
      platform, "clCommandCopyImageToBufferKHR");
  fns.copy_buffer_to_image = resolve<clCommandCopyBufferToImageKHR_fn>(
      platform, "clCommandCopyBufferToImageKHR");
  fns.fill_image = resolve<clCommandFillImageKHR_fn>(
      platform, "clCommandFillImageKHR");

  if (!fns.create || !fns.finalize || !fns.release || !fns.ndrange)
    fns = command_buffer_fns{};

  auto inserted = platform_fns().emplace(platform, fns).first;
  return inserted->second.create ? &inserted->second : nullptr;
}

std::shared_ptr<recording_session> find_session(cl_command_queue queue) {
  auto it = sessions().find(queue);
  return it == sessions().end() ? nullptr : it->second;
}

// Finds the session that produced `event`, if any.
std::shared_ptr<recording_session> session_of_event(cl_event event) {
  for (const auto &entry : sessions()) {
    if (entry.second->event_sync_points.count(event))
      return entry.second;
  }
  return nullptr;
}

// Transitive queue recording, following the semantics of the SYCL graph
// record and replay extension: a queue that is not recording, but which
// submits a command depending on an event returned by a recording queue,
// enters the recording state of that session before the command is submitted,
// and stays in it until the recording ends. The recording properties of the
// session are inherited, since the queue joins the very same command-buffer.
//
// The queue list of a command-buffer is fixed at creation time, so a
// transitively recording queue is attributed to the command-buffer queue of
// the same device.
cl_int join_session(const std::shared_ptr<recording_session> &session,
                    cl_command_queue queue) {
  cl_device_id device = queue_device(queue);
  bool found = false;
  cl_command_queue command_queue = nullptr;
  for (cl_command_queue candidate : session->queues) {
    auto it = session->command_queues.find(candidate);
    if (it != session->command_queues.end() &&
        queue_device(candidate) == device) {
      command_queue = it->second;
      found = true;
      break;
    }
  }
  if (!found)
    return CL_INVALID_COMMAND_QUEUE;

  session->queues.push_back(queue);
  session->command_queues[queue] = command_queue;
  // Several queues now feed the same command-buffer queue, so the implicit
  // in-order recording semantics are no longer sufficient to express the
  // dependencies of the recorded commands.
  session->explicit_sync = true;
  sessions()[queue] = session;
  return CL_SUCCESS;
}

// Returns the session `queue` records into, joining an ongoing session
// transitively when the command depends on an event of that session.
std::shared_ptr<recording_session>
acquire_session(cl_command_queue queue, cl_uint num_events,
                const cl_event *event_wait_list, cl_int &error) {
  error = CL_SUCCESS;
  auto session = find_session(queue);
  if (session)
    return session;

  for (cl_uint i = 0; i < num_events && event_wait_list; ++i) {
    session = session_of_event(event_wait_list[i]);
    if (!session)
      continue;
    error = join_session(session, queue);
    return error == CL_SUCCESS ? session : nullptr;
  }
  return nullptr;
}

// Creates the placeholder event handed back to the application for a recorded
// command. The event is never signalled: it only exists so that the
// application can express dependencies between recorded commands, exactly like
// it would for a regular enqueue.
cl_event make_recording_event(recording_session &session,
                              cl_command_queue queue,
                              cl_sync_point_khr sync_point) {
  cl_context context = queue_context(queue);
  if (!context)
    return nullptr;
  cl_int error = CL_SUCCESS;
  cl_event event = tdispatch->clCreateUserEvent(context, &error);
  if (error != CL_SUCCESS || !event)
    return nullptr;
  session.event_sync_points[event] = sync_point;
  return event;
}

// Translates an event wait list into the sync point wait list of the
// command-buffer being recorded.
cl_int translate_wait_list(recording_session &session, cl_uint num_events,
                           const cl_event *event_wait_list,
                           std::vector<cl_sync_point_khr> &sync_points) {
  if ((num_events && !event_wait_list) || (!num_events && event_wait_list))
    return CL_INVALID_EVENT_WAIT_LIST;
  for (cl_uint i = 0; i < num_events; ++i) {
    auto it = session.event_sync_points.find(event_wait_list[i]);
    if (it == session.event_sync_points.end())
      // Waiting on an event that was not produced by this recording session
      // cannot be expressed inside a command-buffer.
      return CL_INVALID_EVENT_WAIT_LIST;
    sync_points.push_back(it->second);
  }
  return CL_SUCCESS;
}

// Common recording path: translates the wait list, calls the clCommand*KHR
// entry point through `record`, and produces the placeholder event.
template <typename RecordFn>
cl_int record_command(recording_session &session, cl_command_queue queue,
                      cl_uint num_events, const cl_event *event_wait_list,
                      cl_event *event, RecordFn record) {
  std::vector<cl_sync_point_khr> sync_points;
  cl_int error = translate_wait_list(session, num_events, event_wait_list,
                                     sync_points);
  if (error != CL_SUCCESS)
    return error;

  // In-order queues get their ordering implicitly from the command-buffer, so
  // a sync point is only materialized when the application asks for an event
  // or when an explicit dependency has to be expressed.
  const bool needs_sync_point = event != nullptr || session.explicit_sync;
  cl_sync_point_khr sync_point = 0;
  error = record(static_cast<cl_uint>(sync_points.size()),
                 sync_points.empty() ? nullptr : sync_points.data(),
                 needs_sync_point ? &sync_point : nullptr);
  if (error != CL_SUCCESS)
    return error;

  ++session.num_commands;

  if (event) {
    cl_event recorded = make_recording_event(session, queue, sync_point);
    if (!recorded)
      return CL_OUT_OF_RESOURCES;
    *event = recorded;
  }
  return CL_SUCCESS;
}

void destroy_session_events(recording_session &session) {
  for (auto &entry : session.event_sync_points)
    tdispatch->clReleaseEvent(entry.first);
  session.event_sync_points.clear();
}

// Finalizes the command-buffer of `session` and takes every participating
// queue out of the recording state. Ownership of the command-buffer is
// transferred to the caller.
cl_int finalize_session(const std::shared_ptr<recording_session> &session,
                        cl_command_buffer_khr *command_buffer_ret) {
  cl_int error = session->fns->finalize(session->command_buffer);
  if (error != CL_SUCCESS)
    return error;

  destroy_session_events(*session);
  for (cl_command_queue queue : session->queues)
    sessions().erase(queue);

  *command_buffer_ret = session->command_buffer;
  return CL_SUCCESS;
}

void store_finalized(const std::shared_ptr<recording_session> &session,
                     cl_command_buffer_khr command_buffer) {
  for (cl_command_queue queue : session->queues) {
    auto previous = finalized().find(queue);
    if (previous != finalized().end())
      session->fns->release(previous->second);
    if (queue != session->queues.front())
      session->fns->retain(command_buffer);
    finalized()[queue] = command_buffer;
  }
}

} // namespace

/******************************************************************************
 * Queue recording entry points, reachable through
 * clGetExtensionFunctionAddressForPlatform().
 *****************************************************************************/

static CL_API_ENTRY cl_int CL_API_CALL clBeginRecordingCommandBufferLAYER(
    cl_uint num_queues, const cl_command_queue *queues,
    const cl_command_buffer_properties_khr *properties) {
  if (!num_queues || !queues)
    return CL_INVALID_VALUE;

  std::lock_guard<std::mutex> lock(mutex());

  for (cl_uint i = 0; i < num_queues; ++i) {
    if (!queues[i] || find_session(queues[i]))
      return CL_INVALID_COMMAND_QUEUE;
  }

  const command_buffer_fns *fns = get_fns(queue_platform(queues[0]));
  if (!fns)
    // The implementation does not support cl_khr_command_buffer.
    return CL_INVALID_OPERATION;

  cl_int error = CL_SUCCESS;
  cl_command_buffer_khr command_buffer =
      fns->create(num_queues, queues, properties, &error);
  if (error != CL_SUCCESS || !command_buffer)
    return error != CL_SUCCESS ? error : CL_OUT_OF_RESOURCES;

  auto session = std::make_shared<recording_session>();
  session->command_buffer = command_buffer;
  session->fns = fns;
  session->queues.assign(queues, queues + num_queues);
  session->explicit_sync = num_queues > 1 || queue_is_out_of_order(queues[0]);

  for (cl_command_queue queue : session->queues) {
    // A command-buffer created with a single queue accepts NULL as the
    // command-queue of its commands.
    session->command_queues[queue] = num_queues > 1 ? queue : nullptr;
    sessions()[queue] = session;
  }

  return CL_SUCCESS;
}

static CL_API_ENTRY cl_int CL_API_CALL clEndRecordingCommandBufferLAYER(
    cl_command_queue command_queue, cl_command_buffer_khr *command_buffer_ret) {
  if (!command_buffer_ret)
    return CL_INVALID_VALUE;

  std::lock_guard<std::mutex> lock(mutex());
  auto session = find_session(command_queue);
  if (!session)
    return CL_INVALID_OPERATION;

  return finalize_session(session, command_buffer_ret);
}

static CL_API_ENTRY cl_int CL_API_CALL clGetRecordedCommandBufferLAYER(
    cl_command_queue command_queue, cl_command_buffer_khr *command_buffer_ret) {
  if (!command_buffer_ret)
    return CL_INVALID_VALUE;

  std::lock_guard<std::mutex> lock(mutex());
  auto it = finalized().find(command_queue);
  if (it == finalized().end())
    return CL_INVALID_OPERATION;

  const command_buffer_fns *fns = get_fns(queue_platform(command_queue));
  if (!fns)
    return CL_INVALID_OPERATION;

  cl_int error = fns->retain(it->second);
  if (error != CL_SUCCESS)
    return error;
  *command_buffer_ret = it->second;
  return CL_SUCCESS;
}

static CL_API_ENTRY cl_int CL_API_CALL clGetCommandQueueRecordingInfoLAYER(
    cl_command_queue command_queue, cl_queue_recording_info_layer param_name,
    size_t param_value_size, void *param_value, size_t *param_value_size_ret) {
  std::lock_guard<std::mutex> lock(mutex());
  auto session = find_session(command_queue);
  auto done = finalized().find(command_queue);

  switch (param_name) {
  case CL_QUEUE_RECORDING_STATE_LAYER: {
    cl_queue_recording_state_layer state =
        session ? CL_QUEUE_RECORDING_STATE_RECORDING_LAYER
                : (done != finalized().end()
                       ? CL_QUEUE_RECORDING_STATE_FINALIZED_LAYER
                       : CL_QUEUE_RECORDING_STATE_NONE_LAYER);
    if (param_value) {
      if (param_value_size < sizeof(state))
        return CL_INVALID_VALUE;
      std::memcpy(param_value, &state, sizeof(state));
    }
    if (param_value_size_ret)
      *param_value_size_ret = sizeof(state);
    return CL_SUCCESS;
  }
  case CL_QUEUE_RECORDED_COMMAND_BUFFER_LAYER: {
    if (done == finalized().end())
      return CL_INVALID_OPERATION;
    if (param_value) {
      if (param_value_size < sizeof(cl_command_buffer_khr))
        return CL_INVALID_VALUE;
      std::memcpy(param_value, &done->second, sizeof(cl_command_buffer_khr));
    }
    if (param_value_size_ret)
      *param_value_size_ret = sizeof(cl_command_buffer_khr);
    return CL_SUCCESS;
  }
  case CL_QUEUE_RECORDED_COMMAND_COUNT_LAYER: {
    cl_uint count =
        session ? static_cast<cl_uint>(session->num_commands) : 0u;
    if (param_value) {
      if (param_value_size < sizeof(count))
        return CL_INVALID_VALUE;
      std::memcpy(param_value, &count, sizeof(count));
    }
    if (param_value_size_ret)
      *param_value_size_ret = sizeof(count);
    return CL_SUCCESS;
  }
  default:
    return CL_INVALID_VALUE;
  }
}

/******************************************************************************
 * Intercepted entry points.
 *****************************************************************************/

static CL_API_ENTRY cl_int CL_API_CALL clEnqueueNDRangeKernel_wrap(
    cl_command_queue command_queue, cl_kernel kernel, cl_uint work_dim,
    const size_t *global_work_offset, const size_t *global_work_size,
    const size_t *local_work_size, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  std::unique_lock<std::mutex> lock(mutex());
  cl_int error = CL_SUCCESS;
  auto session = acquire_session(command_queue, num_events_in_wait_list,
                                 event_wait_list, error);
  if (!session) {
    if (error != CL_SUCCESS)
      return error;
    lock.unlock();
    return tdispatch->clEnqueueNDRangeKernel(
        command_queue, kernel, work_dim, global_work_offset, global_work_size,
        local_work_size, num_events_in_wait_list, event_wait_list, event);
  }
  const cl_command_queue record_queue = session->command_queue_for(command_queue);

  return record_command(
      *session, command_queue, num_events_in_wait_list, event_wait_list, event,
      [&](cl_uint num_sync_points, const cl_sync_point_khr *sync_point_wait_list,
          cl_sync_point_khr *sync_point) {
        return session->fns->ndrange(session->command_buffer, record_queue,
                                     nullptr, kernel, work_dim,
                                     global_work_offset, global_work_size,
                                     local_work_size, num_sync_points,
                                     sync_point_wait_list, sync_point, nullptr);
      });
}

static CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyBuffer_wrap(
    cl_command_queue command_queue, cl_mem src_buffer, cl_mem dst_buffer,
    size_t src_offset, size_t dst_offset, size_t size,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  std::unique_lock<std::mutex> lock(mutex());
  cl_int error = CL_SUCCESS;
  auto session = acquire_session(command_queue, num_events_in_wait_list,
                                 event_wait_list, error);
  if (!session) {
    if (error != CL_SUCCESS)
      return error;
    lock.unlock();
    return tdispatch->clEnqueueCopyBuffer(command_queue, src_buffer, dst_buffer,
                                          src_offset, dst_offset, size,
                                          num_events_in_wait_list,
                                          event_wait_list, event);
  }
  const cl_command_queue record_queue = session->command_queue_for(command_queue);
  if (!session->fns->copy_buffer)
    return CL_INVALID_OPERATION;

  return record_command(
      *session, command_queue, num_events_in_wait_list, event_wait_list, event,
      [&](cl_uint num_sync_points, const cl_sync_point_khr *sync_point_wait_list,
          cl_sync_point_khr *sync_point) {
        return session->fns->copy_buffer(
            session->command_buffer, record_queue, src_buffer, dst_buffer,
            src_offset, dst_offset, size, num_sync_points,
            sync_point_wait_list, sync_point, nullptr);
      });
}

static CL_API_ENTRY cl_int CL_API_CALL clEnqueueFillBuffer_wrap(
    cl_command_queue command_queue, cl_mem buffer, const void *pattern,
    size_t pattern_size, size_t offset, size_t size,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  std::unique_lock<std::mutex> lock(mutex());
  cl_int error = CL_SUCCESS;
  auto session = acquire_session(command_queue, num_events_in_wait_list,
                                 event_wait_list, error);
  if (!session) {
    if (error != CL_SUCCESS)
      return error;
    lock.unlock();
    return tdispatch->clEnqueueFillBuffer(command_queue, buffer, pattern,
                                          pattern_size, offset, size,
                                          num_events_in_wait_list,
                                          event_wait_list, event);
  }
  const cl_command_queue record_queue = session->command_queue_for(command_queue);
  if (!session->fns->fill_buffer)
    return CL_INVALID_OPERATION;

  return record_command(
      *session, command_queue, num_events_in_wait_list, event_wait_list, event,
      [&](cl_uint num_sync_points, const cl_sync_point_khr *sync_point_wait_list,
          cl_sync_point_khr *sync_point) {
        return session->fns->fill_buffer(
            session->command_buffer, record_queue, buffer, pattern,
            pattern_size, offset, size, num_sync_points, sync_point_wait_list,
            sync_point, nullptr);
      });
}

static CL_API_ENTRY cl_int CL_API_CALL clEnqueueCopyImage_wrap(
    cl_command_queue command_queue, cl_mem src_image, cl_mem dst_image,
    const size_t *src_origin, const size_t *dst_origin, const size_t *region,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  std::unique_lock<std::mutex> lock(mutex());
  cl_int error = CL_SUCCESS;
  auto session = acquire_session(command_queue, num_events_in_wait_list,
                                 event_wait_list, error);
  if (!session) {
    if (error != CL_SUCCESS)
      return error;
    lock.unlock();
    return tdispatch->clEnqueueCopyImage(command_queue, src_image, dst_image,
                                         src_origin, dst_origin, region,
                                         num_events_in_wait_list,
                                         event_wait_list, event);
  }
  const cl_command_queue record_queue = session->command_queue_for(command_queue);
  if (!session->fns->copy_image)
    return CL_INVALID_OPERATION;

  return record_command(
      *session, command_queue, num_events_in_wait_list, event_wait_list, event,
      [&](cl_uint num_sync_points, const cl_sync_point_khr *sync_point_wait_list,
          cl_sync_point_khr *sync_point) {
        return session->fns->copy_image(
            session->command_buffer, record_queue, src_image, dst_image,
            src_origin, dst_origin, region, num_sync_points,
            sync_point_wait_list, sync_point, nullptr);
      });
}

static CL_API_ENTRY cl_int CL_API_CALL clEnqueueBarrierWithWaitList_wrap(
    cl_command_queue command_queue, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  std::unique_lock<std::mutex> lock(mutex());
  cl_int error = CL_SUCCESS;
  auto session = acquire_session(command_queue, num_events_in_wait_list,
                                 event_wait_list, error);
  if (!session) {
    if (error != CL_SUCCESS)
      return error;
    lock.unlock();
    return tdispatch->clEnqueueBarrierWithWaitList(
        command_queue, num_events_in_wait_list, event_wait_list, event);
  }
  const cl_command_queue record_queue = session->command_queue_for(command_queue);
  if (!session->fns->barrier)
    return CL_INVALID_OPERATION;

  return record_command(
      *session, command_queue, num_events_in_wait_list, event_wait_list, event,
      [&](cl_uint num_sync_points, const cl_sync_point_khr *sync_point_wait_list,
          cl_sync_point_khr *sync_point) {
        return session->fns->barrier(session->command_buffer, record_queue,
                                     num_sync_points, sync_point_wait_list,
                                     sync_point, nullptr);
      });
}

// A marker over a set of recorded commands is recorded as a barrier over the
// matching sync points.
static CL_API_ENTRY cl_int CL_API_CALL clEnqueueMarkerWithWaitList_wrap(
    cl_command_queue command_queue, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  std::unique_lock<std::mutex> lock(mutex());
  cl_int error = CL_SUCCESS;
  auto session = acquire_session(command_queue, num_events_in_wait_list,
                                 event_wait_list, error);
  if (!session) {
    if (error != CL_SUCCESS)
      return error;
    lock.unlock();
    return tdispatch->clEnqueueMarkerWithWaitList(
        command_queue, num_events_in_wait_list, event_wait_list, event);
  }
  const cl_command_queue record_queue = session->command_queue_for(command_queue);
  if (!session->fns->barrier)
    return CL_INVALID_OPERATION;

  return record_command(
      *session, command_queue, num_events_in_wait_list, event_wait_list, event,
      [&](cl_uint num_sync_points, const cl_sync_point_khr *sync_point_wait_list,
          cl_sync_point_khr *sync_point) {
        return session->fns->barrier(session->command_buffer, record_queue,
                                     num_sync_points, sync_point_wait_list,
                                     sync_point, nullptr);
      });
}

// Host transfers have no cl_khr_command_buffer counterpart and cannot be
// recorded.
static CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadBuffer_wrap(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read,
    size_t offset, size_t size, void *ptr, cl_uint num_events_in_wait_list,
    const cl_event *event_wait_list, cl_event *event) {
  {
    std::lock_guard<std::mutex> lock(mutex());
    if (find_session(command_queue))
      return CL_INVALID_OPERATION;
  }
  return tdispatch->clEnqueueReadBuffer(command_queue, buffer, blocking_read,
                                        offset, size, ptr,
                                        num_events_in_wait_list,
                                        event_wait_list, event);
}

static CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteBuffer_wrap(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_write,
    size_t offset, size_t size, const void *ptr,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event) {
  {
    std::lock_guard<std::mutex> lock(mutex());
    if (find_session(command_queue))
      return CL_INVALID_OPERATION;
  }
  return tdispatch->clEnqueueWriteBuffer(command_queue, buffer, blocking_write,
                                         offset, size, ptr,
                                         num_events_in_wait_list,
                                         event_wait_list, event);
}

static CL_API_ENTRY void *CL_API_CALL clEnqueueMapBuffer_wrap(
    cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_map,
    cl_map_flags map_flags, size_t offset, size_t size,
    cl_uint num_events_in_wait_list, const cl_event *event_wait_list,
    cl_event *event, cl_int *errcode_ret) {
  {
    std::lock_guard<std::mutex> lock(mutex());
    if (find_session(command_queue)) {
      if (errcode_ret)
        *errcode_ret = CL_INVALID_OPERATION;
      return nullptr;
    }
  }
  return tdispatch->clEnqueueMapBuffer(command_queue, buffer, blocking_map,
                                       map_flags, offset, size,
                                       num_events_in_wait_list, event_wait_list,
                                       event, errcode_ret);
}

// Finishing a recording queue ends the recording: the command-buffer is
// finalized and kept around for clGetRecordedCommandBufferLAYER(). No work was
// ever submitted, so there is nothing to wait for.
static CL_API_ENTRY cl_int CL_API_CALL
clFinish_wrap(cl_command_queue command_queue) {
  std::unique_lock<std::mutex> lock(mutex());
  auto session = find_session(command_queue);
  if (!session) {
    lock.unlock();
    return tdispatch->clFinish(command_queue);
  }

  cl_command_buffer_khr command_buffer = nullptr;
  cl_int error = finalize_session(session, &command_buffer);
  if (error != CL_SUCCESS)
    return error;

  store_finalized(session, command_buffer);
  return CL_SUCCESS;
}

static CL_API_ENTRY cl_int CL_API_CALL
clFlush_wrap(cl_command_queue command_queue) {
  std::unique_lock<std::mutex> lock(mutex());
  if (find_session(command_queue))
    return CL_SUCCESS;
  lock.unlock();
  return tdispatch->clFlush(command_queue);
}

// Placeholder events are never signalled, waiting on them would hang.
static CL_API_ENTRY cl_int CL_API_CALL
clWaitForEvents_wrap(cl_uint num_events, const cl_event *event_list) {
  {
    std::lock_guard<std::mutex> lock(mutex());
    for (const auto &entry : sessions()) {
      for (cl_uint i = 0; i < num_events; ++i) {
        if (entry.second->event_sync_points.count(event_list[i]))
          return CL_INVALID_OPERATION;
      }
    }
  }
  return tdispatch->clWaitForEvents(num_events, event_list);
}

static CL_API_ENTRY cl_int CL_API_CALL
clReleaseCommandQueue_wrap(cl_command_queue command_queue) {
  {
    std::lock_guard<std::mutex> lock(mutex());
    cl_uint references = 0;
    if (tdispatch->clGetCommandQueueInfo(
            command_queue, CL_QUEUE_REFERENCE_COUNT, sizeof(references),
            &references, nullptr) == CL_SUCCESS &&
        references == 1) {
      auto session = find_session(command_queue);
      if (session) {
        destroy_session_events(*session);
        session->fns->release(session->command_buffer);
        for (cl_command_queue queue : session->queues)
          sessions().erase(queue);
      }
      auto done = finalized().find(command_queue);
      if (done != finalized().end()) {
        const command_buffer_fns *fns = get_fns(queue_platform(command_queue));
        if (fns)
          fns->release(done->second);
        finalized().erase(done);
      }
    }
  }
  return tdispatch->clReleaseCommandQueue(command_queue);
}

static CL_API_ENTRY void *CL_API_CALL
clGetExtensionFunctionAddressForPlatform_wrap(cl_platform_id platform,
                                              const char *func_name) {
  if (!func_name)
    return nullptr;

  struct {
    const char *name;
    void *address;
  } layer_functions[] = {
      {"clBeginRecordingCommandBufferLAYER",
       reinterpret_cast<void *>(&clBeginRecordingCommandBufferLAYER)},
      {"clEndRecordingCommandBufferLAYER",
       reinterpret_cast<void *>(&clEndRecordingCommandBufferLAYER)},
      {"clGetRecordedCommandBufferLAYER",
       reinterpret_cast<void *>(&clGetRecordedCommandBufferLAYER)},
      {"clGetCommandQueueRecordingInfoLAYER",
       reinterpret_cast<void *>(&clGetCommandQueueRecordingInfoLAYER)},
  };

  for (const auto &function : layer_functions) {
    if (std::strcmp(function.name, func_name) == 0) {
      std::lock_guard<std::mutex> lock(mutex());
      // Only advertise the recording API if the platform below can actually
      // record command-buffers.
      return get_fns(platform) ? function.address : nullptr;
    }
  }

  return tdispatch->clGetExtensionFunctionAddressForPlatform(platform,
                                                             func_name);
}

void _init_dispatch(void) {
  dispatch.clEnqueueNDRangeKernel = &clEnqueueNDRangeKernel_wrap;
  dispatch.clEnqueueCopyBuffer = &clEnqueueCopyBuffer_wrap;
  dispatch.clEnqueueFillBuffer = &clEnqueueFillBuffer_wrap;
  dispatch.clEnqueueCopyImage = &clEnqueueCopyImage_wrap;
  dispatch.clEnqueueBarrierWithWaitList = &clEnqueueBarrierWithWaitList_wrap;
  dispatch.clEnqueueMarkerWithWaitList = &clEnqueueMarkerWithWaitList_wrap;
  dispatch.clEnqueueReadBuffer = &clEnqueueReadBuffer_wrap;
  dispatch.clEnqueueWriteBuffer = &clEnqueueWriteBuffer_wrap;
  dispatch.clEnqueueMapBuffer = &clEnqueueMapBuffer_wrap;
  dispatch.clFinish = &clFinish_wrap;
  dispatch.clFlush = &clFlush_wrap;
  dispatch.clWaitForEvents = &clWaitForEvents_wrap;
  dispatch.clReleaseCommandQueue = &clReleaseCommandQueue_wrap;
  dispatch.clGetExtensionFunctionAddressForPlatform =
      &clGetExtensionFunctionAddressForPlatform_wrap;
}

CL_API_ENTRY cl_int CL_API_CALL clGetLayerInfo(cl_layer_info param_name,
                                               size_t param_value_size,
                                               void *param_value,
                                               size_t *param_value_size_ret) {
  switch (param_name) {
  case CL_LAYER_API_VERSION:
    if (param_value) {
      if (param_value_size < sizeof(cl_layer_api_version))
        return CL_INVALID_VALUE;
      *((cl_layer_api_version *)param_value) = CL_LAYER_API_VERSION_100;
    }
    if (param_value_size_ret)
      *param_value_size_ret = sizeof(cl_layer_api_version);
    break;
  default:
    return CL_INVALID_VALUE;
  }
  return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL
clInitLayer(cl_uint num_entries, const struct _cl_icd_dispatch *target_dispatch,
            cl_uint *num_entries_out,
            const struct _cl_icd_dispatch **layer_dispatch_ret) {
  if (!target_dispatch || !layer_dispatch_ret || !num_entries_out ||
      num_entries < sizeof(dispatch) / sizeof(dispatch.clGetPlatformIDs))
    return CL_INVALID_VALUE;

  _init_dispatch();

  tdispatch = target_dispatch;
  *layer_dispatch_ret = &dispatch;
  *num_entries_out = sizeof(dispatch) / sizeof(dispatch.clGetPlatformIDs);
  return CL_SUCCESS;
}
