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

/* Queue recording API implemented on top of cl_khr_command_buffer.
 *
 * The entry points declared here are not exported by the layer library, they
 * are only reachable through clGetExtensionFunctionAddressForPlatform(), like
 * any other OpenCL extension entry point. */

#ifndef __CL_LAYER_COMMAND_BUFFER_RECORD_H
#define __CL_LAYER_COMMAND_BUFFER_RECORD_H

#include <CL/cl.h>
#include <CL/cl_ext.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CL_LAYER_COMMAND_BUFFER_RECORD_EXTENSION_NAME \
    "cl_layer_command_buffer_record"

typedef cl_uint cl_queue_recording_info_layer;
typedef cl_uint cl_queue_recording_state_layer;

/* cl_queue_recording_info_layer */
#define CL_QUEUE_RECORDING_STATE_LAYER          0x8000
#define CL_QUEUE_RECORDED_COMMAND_BUFFER_LAYER  0x8001
#define CL_QUEUE_RECORDED_COMMAND_COUNT_LAYER   0x8002

/* cl_queue_recording_state_layer */
#define CL_QUEUE_RECORDING_STATE_NONE_LAYER      0
#define CL_QUEUE_RECORDING_STATE_RECORDING_LAYER 1
#define CL_QUEUE_RECORDING_STATE_FINALIZED_LAYER 2

/* Puts `queues` into the recording state. Every command enqueued to a
 * recording queue is recorded into a command-buffer created over `queues`
 * instead of being executed. `properties` is forwarded verbatim to
 * clCreateCommandBufferKHR(). */
typedef cl_int CL_API_CALL
clBeginRecordingCommandBufferLAYER_t(
    cl_uint                                 num_queues,
    const cl_command_queue*                 queues,
    const cl_command_buffer_properties_khr* properties);

typedef clBeginRecordingCommandBufferLAYER_t *
clBeginRecordingCommandBufferLAYER_fn;

/* Ends the recording session `command_queue` takes part in, finalizes the
 * command-buffer and returns it. Ownership of the returned command-buffer is
 * transferred to the caller, which has to release it with
 * clReleaseCommandBufferKHR(). */
typedef cl_int CL_API_CALL
clEndRecordingCommandBufferLAYER_t(
    cl_command_queue       command_queue,
    cl_command_buffer_khr* command_buffer_ret);

/* Returns the command-buffer finalized by the latest clFinish() performed on
 * `command_queue`. The returned command-buffer is retained on behalf of the
 * caller, which has to release it with clReleaseCommandBufferKHR(). */
typedef clEndRecordingCommandBufferLAYER_t *
clEndRecordingCommandBufferLAYER_fn;

typedef cl_int CL_API_CALL
clGetRecordedCommandBufferLAYER_t(
    cl_command_queue       command_queue,
    cl_command_buffer_khr* command_buffer_ret);

typedef clGetRecordedCommandBufferLAYER_t *
clGetRecordedCommandBufferLAYER_fn;

/* Queries the recording state of `command_queue`. */
typedef cl_int CL_API_CALL
clGetCommandQueueRecordingInfoLAYER_t(
    cl_command_queue              command_queue,
    cl_queue_recording_info_layer param_name,
    size_t                        param_value_size,
    void*                         param_value,
    size_t*                       param_value_size_ret);

typedef clGetCommandQueueRecordingInfoLAYER_t *
clGetCommandQueueRecordingInfoLAYER_fn;

#ifdef __cplusplus
}
#endif

#endif /* __CL_LAYER_COMMAND_BUFFER_RECORD_H */
