# Command-buffer recording layer

A draft layer that adds a queue recording API on top of the
[cl_khr_command_buffer](https://registry.khronos.org/OpenCL/specs/unified/html/OpenCL_API.html#cl_khr_command_buffer)
extension.

An application puts one or more command-queues into a *recording* state.
Commands enqueued to a recording queue are not executed, they are recorded
into a command-buffer. Finishing the queue finalizes the command-buffer, which
can then be replayed with `clEnqueueCommandBufferKHR`. This is the OpenCL
equivalent of [CUDA stream
capture](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html#stream-capture)
and of the [SYCL graph record and
replay](https://github.com/intel/llvm/blob/sycl/sycl/doc/extensions/experimental/sycl_ext_oneapi_graph.asciidoc)
API, with the difference that the captured object is a plain
`cl_command_buffer_khr`.

The API is defined in [`cl_layer_command_buffer_record.asciidoc`](cl_layer_command_buffer_record.asciidoc)
and declared in [`cl_layer_command_buffer_record.h`](cl_layer_command_buffer_record.h).

## Usage

```
OPENCL_LAYERS=/path/to/libCLCommandBufferRecordLayer.so /path/to/application
```

The entry points are obtained like any other extension entry point:

```c
clGetExtensionFunctionAddressForPlatform(platform,
                                         "clBeginRecordingCommandBufferLAYER");
```

They are only returned when the underlying platform supports
`cl_khr_command_buffer`, otherwise the layer is a pure pass-through.

## Transitive queue recording

Following the SYCL graph extension, a queue that is not recording but which
enqueues a command depending on an event returned by a recording queue is
transitively put into the recording state of that session, and stays in it
until the recording ends. Because the queue list of a command-buffer is fixed
at creation time, such commands are recorded with the queue of the session
that shares their device.

## Draft status

This is a draft. Known limitations:

  * Host transfers (`clEnqueueReadBuffer`, `clEnqueueWriteBuffer`,
    `clEnqueueMapBuffer`) cannot be recorded and return `CL_INVALID_OPERATION`
    while recording.
  * `clEnqueueCopyBufferRect`, `clEnqueueFillImage`,
    `clEnqueueCopyImageToBuffer`, `clEnqueueCopyBufferToImage` and the SVM
    commands are described by the specification but not wired up yet.
  * The layer does not yet add `cl_layer_command_buffer_record` to the
    platform and device extension strings.
  * There are no tests yet; they require a test ICD implementing
    `cl_khr_command_buffer`, in the style of the object lifetime layer test
    ICD.
