// Copyright (c) 2024 The Khronos Group Inc.
// SPDX-License-Identifier: Apache-2.0
//
// Tests that commands which have no cl_khr_command_buffer counterpart are
// rejected while recording, and that the session stays usable.
//
// REQUIRES: command-buffer
// RUN: %build -o %t
// RUN: %t | FileCheck %s

#include "common.h"

int main() {
  TestFixture Fixture;
  Fixture.setUp();

  cl_int Error = CL_SUCCESS;
  cl_mem Buffer = clCreateBuffer(Fixture.Context, CL_MEM_READ_WRITE,
                                 sizeof(cl_int), nullptr, &Error);
  CL_CHECK(Error);

  CL_CHECK(Fixture.BeginRecording(1, &Fixture.Queue, nullptr));

  cl_int Value = 0;
  // CHECK: read error: -59
  std::printf("read error: %d\n",
              clEnqueueReadBuffer(Fixture.Queue, Buffer, CL_FALSE, 0,
                                  sizeof(Value), &Value, 0, nullptr, nullptr));

  // CHECK-NEXT: map error: -59
  clEnqueueMapBuffer(Fixture.Queue, Buffer, CL_FALSE, CL_MAP_READ, 0,
                     sizeof(Value), 0, nullptr, nullptr, &Error);
  std::printf("map error: %d\n", Error);

  // The rejected commands were not recorded, and the session is still usable.
  CL_CHECK(Fixture.enqueueKernel(Fixture.Queue, 0, nullptr, nullptr));

  cl_uint Count = 0;
  CL_CHECK(Fixture.GetRecordingInfo(Fixture.Queue,
                                    CL_QUEUE_RECORDED_COMMAND_COUNT_LAYER,
                                    sizeof(Count), &Count, nullptr));
  // CHECK-NEXT: recorded: 1
  std::printf("recorded: %u\n", Count);

  cl_command_buffer_khr CommandBuffer = nullptr;
  CL_CHECK(Fixture.EndRecording(Fixture.Queue, &CommandBuffer));

  auto ReleaseCommandBuffer = reinterpret_cast<clReleaseCommandBufferKHR_fn>(
      clGetExtensionFunctionAddressForPlatform(Fixture.Platform,
                                               "clReleaseCommandBufferKHR"));
  CL_CHECK(ReleaseCommandBuffer(CommandBuffer));
  CL_CHECK(clReleaseMemObject(Buffer));

  Fixture.tearDown();
  // CHECK-NEXT: success
  std::printf("success\n");
  return 0;
}
