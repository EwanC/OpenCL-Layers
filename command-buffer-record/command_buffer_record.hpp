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

#ifndef __COMMAND_BUFFER_RECORD_HPP
#define __COMMAND_BUFFER_RECORD_HPP

#include "cl_layer_command_buffer_record.h"

#include <CL/cl_layer.h>

#include <map>
#include <memory>
#include <mutex>
#include <vector>

// The cl_khr_command_buffer entry points the layer records into. They are
// resolved once per platform through the target
// clGetExtensionFunctionAddressForPlatform().
struct command_buffer_fns {
  clCreateCommandBufferKHR_fn create;
  clFinalizeCommandBufferKHR_fn finalize;
  clRetainCommandBufferKHR_fn retain;
  clReleaseCommandBufferKHR_fn release;
  clCommandBarrierWithWaitListKHR_fn barrier;
  clCommandNDRangeKernelKHR_fn ndrange;
  clCommandCopyBufferKHR_fn copy_buffer;
  clCommandCopyBufferRectKHR_fn copy_buffer_rect;
  clCommandFillBufferKHR_fn fill_buffer;
  clCommandCopyImageKHR_fn copy_image;
  clCommandCopyImageToBufferKHR_fn copy_image_to_buffer;
  clCommandCopyBufferToImageKHR_fn copy_buffer_to_image;
  clCommandFillImageKHR_fn fill_image;
};

// State of a single recording session: the command-buffer being recorded, the
// queues taking part in it, and the mapping from the placeholder events handed
// back to the application to the sync points of the command-buffer.
struct recording_session {
  cl_command_buffer_khr command_buffer = nullptr;
  const command_buffer_fns *fns = nullptr;

  // Queues in the recording state, either because they were passed to
  // clBeginRecordingCommandBufferLAYER() or because they were transitively put
  // into the recording state.
  std::vector<cl_command_queue> queues;

  // Queue a recorded command is attributed to in the command-buffer. This is
  // the queue itself for queues the command-buffer was created with, and the
  // command-buffer queue of the same device for transitively recorded queues,
  // since the queue list of a command-buffer is fixed at creation time.
  std::map<cl_command_queue, cl_command_queue> command_queues;

  std::map<cl_event, cl_sync_point_khr> event_sync_points;

  // Whether dependencies have to be expressed with explicit sync points rather
  // than relying on the implicit in-order recording semantics.
  bool explicit_sync = false;

  size_t num_commands = 0;

  cl_command_queue command_queue_for(cl_command_queue queue) const {
    auto it = command_queues.find(queue);
    return it == command_queues.end() ? nullptr : it->second;
  }
};

extern struct _cl_icd_dispatch dispatch;
extern const struct _cl_icd_dispatch *tdispatch;

extern void _init_dispatch(void);

#endif /* __COMMAND_BUFFER_RECORD_HPP */
