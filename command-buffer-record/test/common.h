// Copyright (c) 2025-2026 Ewan Crawford

#ifndef __TEST_COMMON_H
#define __TEST_COMMON_H

#include <CL/cl.h>
#include <cl_layer_command_buffer_record.h>

#include <cstdio>
#include <cstdlib>

#define CL_CHECK(X)                                                            \
  do {                                                                         \
    const cl_int CheckError = (X);                                             \
    if (CheckError != CL_SUCCESS) {                                            \
      std::printf("%s:%d: %s failed: %d\n", __FILE__, __LINE__, #X,            \
                  CheckError);                                                 \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

// Boilerplate shared by the tests: creates a context and a command-queue on
// the first device of the first platform, and resolves the layer entry points.
struct TestFixture {
  cl_platform_id Platform = nullptr;
  cl_device_id Device = nullptr;
  cl_context Context = nullptr;
  cl_command_queue Queue = nullptr;
  cl_program Program = nullptr;
  cl_kernel Kernel = nullptr;

  clBeginRecordingCommandBufferLAYER_fn BeginRecording = nullptr;
  clEndRecordingCommandBufferLAYER_fn EndRecording = nullptr;
  clGetRecordedCommandBufferLAYER_fn GetRecorded = nullptr;
  clGetCommandQueueRecordingInfoLAYER_fn GetRecordingInfo = nullptr;

  void setUp(bool WithKernel = true) {
    CL_CHECK(clGetPlatformIDs(1, &Platform, nullptr));
    CL_CHECK(
        clGetDeviceIDs(Platform, CL_DEVICE_TYPE_DEFAULT, 1, &Device, nullptr));

    cl_int Error = CL_SUCCESS;
    Context = clCreateContext(nullptr, 1, &Device, nullptr, nullptr, &Error);
    CL_CHECK(Error);
    Queue = clCreateCommandQueue(Context, Device, 0, &Error);
    CL_CHECK(Error);

    BeginRecording = reinterpret_cast<clBeginRecordingCommandBufferLAYER_fn>(
        clGetExtensionFunctionAddressForPlatform(
            Platform, "clBeginRecordingCommandBufferLAYER"));
    EndRecording = reinterpret_cast<clEndRecordingCommandBufferLAYER_fn>(
        clGetExtensionFunctionAddressForPlatform(
            Platform, "clEndRecordingCommandBufferLAYER"));
    GetRecorded = reinterpret_cast<clGetRecordedCommandBufferLAYER_fn>(
        clGetExtensionFunctionAddressForPlatform(
            Platform, "clGetRecordedCommandBufferLAYER"));
    GetRecordingInfo = reinterpret_cast<clGetCommandQueueRecordingInfoLAYER_fn>(
        clGetExtensionFunctionAddressForPlatform(
            Platform, "clGetCommandQueueRecordingInfoLAYER"));

    if (!BeginRecording || !EndRecording || !GetRecorded || !GetRecordingInfo) {
      std::printf("Layer entry points not found\n");
      std::exit(1);
    }

    if (WithKernel)
      buildKernel();
  }

  void buildKernel() {
    const char *Source = "kernel void empty() {}";
    cl_int Error = CL_SUCCESS;
    Program = clCreateProgramWithSource(Context, 1, &Source, nullptr, &Error);
    CL_CHECK(Error);
    CL_CHECK(clBuildProgram(Program, 1, &Device, nullptr, nullptr, nullptr));
    Kernel = clCreateKernel(Program, "empty", &Error);
    CL_CHECK(Error);
  }

  void tearDown() {
    if (Kernel)
      CL_CHECK(clReleaseKernel(Kernel));
    if (Program)
      CL_CHECK(clReleaseProgram(Program));
    CL_CHECK(clReleaseCommandQueue(Queue));
    CL_CHECK(clReleaseContext(Context));
  }

  cl_int enqueueKernel(cl_command_queue Q, cl_uint NumEvents,
                       const cl_event *WaitList, cl_event *Event) {
    const size_t GlobalSize = 1;
    return clEnqueueNDRangeKernel(Q, Kernel, 1, nullptr, &GlobalSize, nullptr,
                                  NumEvents, WaitList, Event);
  }
};

#endif /* __TEST_COMMON_H */
