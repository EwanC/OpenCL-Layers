// Copyright (c) 2024 The Khronos Group Inc.
// SPDX-License-Identifier: Apache-2.0
//
// Tests clEndRecordingCommandBufferLAYER() as an alternative to clFinish().
//
// REQUIRES: command-buffer
// RUN: %build -o %t
// RUN: %t | FileCheck %s

#include "common.h"

int main() {
  TestFixture Fixture;
  Fixture.setUp();

  CL_CHECK(Fixture.BeginRecording(1, &Fixture.Queue, nullptr));
  CL_CHECK(Fixture.enqueueKernel(Fixture.Queue, 0, nullptr, nullptr));

  cl_command_buffer_khr CommandBuffer = nullptr;
  CL_CHECK(Fixture.EndRecording(Fixture.Queue, &CommandBuffer));

  // The queue is no longer recording, so this kernel is executed.
  CL_CHECK(Fixture.enqueueKernel(Fixture.Queue, 0, nullptr, nullptr));
  CL_CHECK(clFinish(Fixture.Queue));

  cl_queue_recording_state_layer State = CL_QUEUE_RECORDING_STATE_NONE_LAYER;
  CL_CHECK(Fixture.GetRecordingInfo(Fixture.Queue,
                                    CL_QUEUE_RECORDING_STATE_LAYER,
                                    sizeof(State), &State, nullptr));
  // CHECK: state: 0
  std::printf("state: %u\n", State);

  // Ending recording on a queue that is not recording is an error.
  cl_command_buffer_khr Unused = nullptr;
  // CHECK-NEXT: end error: -59
  std::printf("end error: %d\n", Fixture.EndRecording(Fixture.Queue, &Unused));

  auto ReleaseCommandBuffer = reinterpret_cast<clReleaseCommandBufferKHR_fn>(
      clGetExtensionFunctionAddressForPlatform(Fixture.Platform,
                                               "clReleaseCommandBufferKHR"));
  CL_CHECK(ReleaseCommandBuffer(CommandBuffer));

  Fixture.tearDown();
  // CHECK-NEXT: success
  std::printf("success\n");
  return 0;
}
