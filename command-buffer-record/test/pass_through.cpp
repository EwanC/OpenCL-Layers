// Copyright (c) 2024 The Khronos Group Inc.
// SPDX-License-Identifier: Apache-2.0
//
// Tests that queues which are not recording are unaffected by the layer.
//
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

  const cl_int Pattern = 42;
  CL_CHECK(clEnqueueFillBuffer(Fixture.Queue, Buffer, &Pattern, sizeof(Pattern),
                               0, sizeof(Pattern), 0, nullptr, nullptr));
  CL_CHECK(Fixture.enqueueKernel(Fixture.Queue, 0, nullptr, nullptr));

  cl_int Result = 0;
  CL_CHECK(clEnqueueReadBuffer(Fixture.Queue, Buffer, CL_TRUE, 0,
                               sizeof(Result), &Result, 0, nullptr, nullptr));
  CL_CHECK(clFinish(Fixture.Queue));
  // CHECK: result: 42
  std::printf("result: %d\n", Result);

  cl_queue_recording_state_layer State =
      CL_QUEUE_RECORDING_STATE_RECORDING_LAYER;
  CL_CHECK(Fixture.GetRecordingInfo(Fixture.Queue,
                                    CL_QUEUE_RECORDING_STATE_LAYER,
                                    sizeof(State), &State, nullptr));
  // CHECK-NEXT: state: 0
  std::printf("state: %u\n", State);

  CL_CHECK(clReleaseMemObject(Buffer));
  Fixture.tearDown();
  // CHECK-NEXT: success
  std::printf("success\n");
  return 0;
}
