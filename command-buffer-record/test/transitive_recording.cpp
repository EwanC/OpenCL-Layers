// Copyright (c) 2025-2026 Ewan Crawford
//
// Tests that a queue which is not recording transitively enters the recording
// state when it enqueues a command depending on an event returned by a
// recording queue.
//
// REQUIRES: command-buffer
// RUN: %build -o %t
// RUN: %t | FileCheck %s

#include "common.h"

int main() {
  TestFixture Fixture;
  Fixture.setUp();

  cl_int Error = CL_SUCCESS;
  cl_command_queue SecondQueue =
      clCreateCommandQueue(Fixture.Context, Fixture.Device, 0, &Error);
  CL_CHECK(Error);

  CL_CHECK(Fixture.BeginRecording(1, &Fixture.Queue, nullptr));

  cl_event Event = nullptr;
  CL_CHECK(Fixture.enqueueKernel(Fixture.Queue, 0, nullptr, &Event));

  cl_queue_recording_state_layer State = CL_QUEUE_RECORDING_STATE_NONE_LAYER;
  CL_CHECK(Fixture.GetRecordingInfo(SecondQueue, CL_QUEUE_RECORDING_STATE_LAYER,
                                    sizeof(State), &State, nullptr));
  // CHECK: second queue state: 0
  std::printf("second queue state: %u\n", State);

  // Depends on an event of the recording session, so the second queue joins it.
  CL_CHECK(Fixture.enqueueKernel(SecondQueue, 1, &Event, nullptr));

  CL_CHECK(Fixture.GetRecordingInfo(SecondQueue, CL_QUEUE_RECORDING_STATE_LAYER,
                                    sizeof(State), &State, nullptr));
  // CHECK-NEXT: second queue state: 1
  std::printf("second queue state: %u\n", State);

  cl_uint Count = 0;
  CL_CHECK(Fixture.GetRecordingInfo(SecondQueue,
                                    CL_QUEUE_RECORDED_COMMAND_COUNT_LAYER,
                                    sizeof(Count), &Count, nullptr));
  // CHECK-NEXT: recorded: 2
  std::printf("recorded: %u\n", Count);

  cl_command_buffer_khr CommandBuffer = nullptr;
  CL_CHECK(Fixture.EndRecording(Fixture.Queue, &CommandBuffer));

  // Both queues left the recording state when the session ended.
  CL_CHECK(Fixture.GetRecordingInfo(SecondQueue, CL_QUEUE_RECORDING_STATE_LAYER,
                                    sizeof(State), &State, nullptr));
  // CHECK-NEXT: second queue state: 0
  std::printf("second queue state: %u\n", State);

  auto ReleaseCommandBuffer = reinterpret_cast<clReleaseCommandBufferKHR_fn>(
      clGetExtensionFunctionAddressForPlatform(Fixture.Platform,
                                               "clReleaseCommandBufferKHR"));
  CL_CHECK(ReleaseCommandBuffer(CommandBuffer));
  CL_CHECK(clReleaseCommandQueue(SecondQueue));

  Fixture.tearDown();
  // CHECK-NEXT: success
  std::printf("success\n");
  return 0;
}
