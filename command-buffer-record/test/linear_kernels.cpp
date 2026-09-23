// Copyright (c) 2024 The Khronos Group Inc.
// SPDX-License-Identifier: Apache-2.0
//
// Tests that commands enqueued to a recording queue are recorded rather than
// executed, and that clFinish() finalizes the command-buffer.
//
// REQUIRES: command-buffer
// RUN: %build -o %t
// RUN: %t | FileCheck %s

#include "common.h"

int main() {
  TestFixture Fixture;
  Fixture.setUp();

  CL_CHECK(Fixture.BeginRecording(1, &Fixture.Queue, nullptr));

  cl_queue_recording_state_layer State = CL_QUEUE_RECORDING_STATE_NONE_LAYER;
  CL_CHECK(Fixture.GetRecordingInfo(Fixture.Queue,
                                    CL_QUEUE_RECORDING_STATE_LAYER,
                                    sizeof(State), &State, nullptr));
  // CHECK: state: 1
  std::printf("state: %u\n", State);

  CL_CHECK(Fixture.enqueueKernel(Fixture.Queue, 0, nullptr, nullptr));
  CL_CHECK(Fixture.enqueueKernel(Fixture.Queue, 0, nullptr, nullptr));

  cl_uint Count = 0;
  CL_CHECK(Fixture.GetRecordingInfo(Fixture.Queue,
                                    CL_QUEUE_RECORDED_COMMAND_COUNT_LAYER,
                                    sizeof(Count), &Count, nullptr));
  // CHECK-NEXT: recorded: 2
  std::printf("recorded: %u\n", Count);

  // Ends the recording session, no work was submitted so this doesn't block.
  CL_CHECK(clFinish(Fixture.Queue));

  CL_CHECK(Fixture.GetRecordingInfo(Fixture.Queue,
                                    CL_QUEUE_RECORDING_STATE_LAYER,
                                    sizeof(State), &State, nullptr));
  // CHECK-NEXT: state: 2
  std::printf("state: %u\n", State);

  cl_command_buffer_khr CommandBuffer = nullptr;
  CL_CHECK(Fixture.GetRecorded(Fixture.Queue, &CommandBuffer));

  auto EnqueueCommandBuffer = reinterpret_cast<clEnqueueCommandBufferKHR_fn>(
      clGetExtensionFunctionAddressForPlatform(Fixture.Platform,
                                               "clEnqueueCommandBufferKHR"));
  auto ReleaseCommandBuffer = reinterpret_cast<clReleaseCommandBufferKHR_fn>(
      clGetExtensionFunctionAddressForPlatform(Fixture.Platform,
                                               "clReleaseCommandBufferKHR"));

  // Replaying the recorded command-buffer executes the recorded kernels.
  CL_CHECK(
      EnqueueCommandBuffer(0, nullptr, CommandBuffer, 0, nullptr, nullptr));
  CL_CHECK(clFinish(Fixture.Queue));
  CL_CHECK(ReleaseCommandBuffer(CommandBuffer));

  Fixture.tearDown();
  // CHECK-NEXT: success
  std::printf("success\n");
  return 0;
}
