# CLRecordLayer

[![Build & Test](https://github.com/EwanC/OpenCL-Layers/actions/workflows/build_test.yml/badge.svg)](https://github.com/EwanC/OpenCL-Layers/actions/workflows/build_test.yml)

OpenCL [layer](https://github.com/KhronosGroup/OpenCL-ICD-Loader?tab=readme-ov-file#about-layers)
that implements a queue recording API on top of the
[cl_khr_command_buffer](https://registry.khronos.org/OpenCL/specs/unified/html/OpenCL_API.html#cl_khr_command_buffer)
extension.

A user puts a command-queue into a *recording* state, after which the commands
enqueued to it with the regular `clEnqueue*` entry points are no longer
executed, they are recorded into a command-buffer instead. Finishing the queue
finalizes the command-buffer, which is then returned to the user and can be
replayed any number of times with `clEnqueueCommandBufferKHR`.

This is the OpenCL equivalent of
[CUDA stream capture](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html#stream-capture)
and of the
[SYCL graph record and replay](https://github.com/intel/llvm/blob/sycl/sycl/doc/extensions/experimental/sycl_ext_oneapi_graph.asciidoc)
API, with the difference that the captured object is a plain
`cl_command_buffer_khr`, usable with the unmodified Khronos extension:

```c
cl_command_buffer_khr CommandBuffer = NULL;

clBeginRecordingCommandBufferLAYER(1, &Queue, NULL);

// Recorded, not executed.
clEnqueueFillBuffer(Queue, Buffer, &Pattern, sizeof(Pattern), 0, Size,
                    0, NULL, NULL);
clEnqueueNDRangeKernel(Queue, Kernel, 1, NULL, &GlobalSize, NULL,
                       0, NULL, NULL);

// Ends the recording session and finalizes the command-buffer.
clFinish(Queue);
clGetRecordedCommandBufferLAYER(Queue, &CommandBuffer);

// Replay.
for (int i = 0; i < Iterations; ++i)
  clEnqueueCommandBufferKHR(0, NULL, CommandBuffer, 0, NULL, NULL);

clFinish(Queue);
clReleaseCommandBufferKHR(CommandBuffer);
```

The API is specified in [doc/cl_layer_command_buffer_record.asciidoc](doc/cl_layer_command_buffer_record.asciidoc)
and declared in [include/cl_layer_command_buffer_record.h](include/cl_layer_command_buffer_record.h).

*For more information on OpenCL Layers see [The OpenCL Layers Tutorial](https://github.com/Kerilk/OpenCL-Layers-Tutorial).*

## Building

OpenCL layers are shared libraries, using the following instructions the layer
will be built as `libCLRecordLayer.so`/`CLRecordLayer.dll`.

This directory is a self contained CMake project, so it can be configured
directly as the root of the build, either from its own repository or from
inside the [OpenCL-Layers](https://github.com/EwanC/OpenCL-Layers) collection:

```sh
$ git clone https://github.com/EwanC/OpenCL-Layers
$ cmake -S OpenCL-Layers/command-buffer-record -B build
$ cmake --build ./build
```

When instead it is configured as a sub-directory of the OpenCL-Layers
collection, the layer is built using the OpenCL headers and common compile
options provided by the parent project, and the tests documented below are not
added.

The project requires [OpenCL-Headers](https://github.com/KhronosGroup/OpenCL-Headers)
and [OpenCL-ICD-Loader](https://github.com/KhronosGroup/OpenCL-ICD-Loader) to
build against. By default it pulls these using `FetchContent` from the `main`
branch on GitHub to get the latest code.

However, if a build without downloading dependencies is preferred then
`REC_OFFLINE_BUILD` can be set during CMake configure. Then
`find_package(OpenCLHeaders)` and `find_package(OpenCLICDLoader)` will be used
to detect local installations of the packages. Search paths to these can be
passed with `CMAKE_PREFIX_PATH`:

```sh
$ cmake -S OpenCL-Layers/command-buffer-record -B build \
    -DREC_OFFLINE_BUILD=ON \
    -DCMAKE_PREFIX_PATH="/path/to/OpenCL-Headers/install;/path/to/OpenCL-ICD-Loader/install"
```

Setting `REC_DEBUG` during CMake configure enables debug logging of the
recording sessions to `stderr`.

## Running

Layers are loaded by the OpenCL ICD loader, so applications have to use the
Khronos loader to be able to use the layer:

```sh
$ OPENCL_LAYERS=/path/to/libCLRecordLayer.so ./application
```

If the system ICD loader is not the Khronos loader, or does not have layer
support enabled, the loader built by this project can be used instead:

```sh
$ LD_PRELOAD=/path/to/build/_deps/opencl-icd-loader-build/libOpenCL.so \
  OPENCL_LAYERS=/path/to/libCLRecordLayer.so ./application
```

The recording entry points are obtained like any other OpenCL extension entry
point:

```c
clGetExtensionFunctionAddressForPlatform(Platform,
                                         "clBeginRecordingCommandBufferLAYER");
```

They are only returned when the platform below the layer supports
`cl_khr_command_buffer`, otherwise the layer is a pure pass-through.

## API

| Entry point | Description |
| ----------- | ----------- |
| `clBeginRecordingCommandBufferLAYER` | Puts command-queues into the recording state. |
| `clEndRecordingCommandBufferLAYER`   | Ends the recording session and returns the finalized command-buffer. |
| `clGetRecordedCommandBufferLAYER`    | Returns the command-buffer finalized by the latest `clFinish()`. |
| `clGetCommandQueueRecordingInfoLAYER`| Queries the recording state of a command-queue. |

Following the SYCL graph extension, queue recording is *transitive*: a queue
that is not recording, but which enqueues a command depending on an event
returned by a recording queue, joins that recording session and stays in it
until the recording ends.

Commands that have no counterpart in `cl_khr_command_buffer`, such as the host
transfers `clEnqueueReadBuffer`, `clEnqueueWriteBuffer` and
`clEnqueueMapBuffer`, return `CL_INVALID_OPERATION` while recording.

## Tests

Tests are written as OpenCL applications run under the layer, using
[lit](https://llvm.org/docs/CommandGuide/lit.html) and
[FileCheck](https://llvm.org/docs/CommandGuide/FileCheck.html) to verify their
output. These dependencies, as well as `clinfo`, need to be in `PATH` for the
tests to be enabled:

```sh
$ pip install -r requirements.txt
$ sudo apt install clinfo
```

The tests are then run with the `check` target:

```sh
$ cmake --build ./build --target check
```

Tests which need an implementation supporting `cl_khr_command_buffer` are
skipped when the first device of the first platform does not report the
extension. [POCL](https://github.com/pocl/pocl) is used in CI for this purpose.

## Draft status

This is a draft, known limitations are:

  * `clEnqueueCopyBufferRect`, `clEnqueueFillImage`, `clEnqueueCopyImageToBuffer`,
    `clEnqueueCopyBufferToImage` and the SVM commands are described by the
    specification but not wired up yet.
  * The layer does not yet add `cl_layer_command_buffer_record` to the platform
    and device extension strings.
