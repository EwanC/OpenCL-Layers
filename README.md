# OpenCL-Layers
A collection of OpenCL layers.

## Layers

* `simple-print`: traces OpenCL API calls.
* `ocl-icd-compat`: ocl-icd compatibility layer.
* `object-lifetime`: checks the lifetime of OpenCL objects.
* `param-verification`: validates the arguments of OpenCL API calls.
* `command-buffer-record`: adds a queue recording API on top of
  `cl_khr_command_buffer` (draft, see
  [command-buffer-record/README.md](command-buffer-record/README.md)). This
  directory is self contained, holding its own CMake project, documentation,
  tests and CI workflow, so that it can be moved to a standalone repository.

## Building

The layers in this repository can be built using CMake:

```
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ../
make
```

## Using a layer

Layers are loaded and enabled by the
[OpenCL-ICD-Loader](https://github.com/KhronosGroup/OpenCL-ICD-Loader) so
applications have to use the ICD loader to be able to use layers.

On a Unix-like system, a typical usage looks like the following (taking the
example of the object lifetime layer):

```
OPENCL_LAYERS=/path/to/OpenCL-Layers/build/object-lifetime/libCLObjectLifetimeLayer.so \
/path/to/application
```
This assumes that the the OpenCL-ICD-Loader is already installed on the system and
configured to point to available ICDs (i.e. OpenCL implementation shared libraries).

If this is not the case, please refer to the full OpenCL-ICD-Loader documentation
for how to install and configure the loader.

The following should however be useful in many scenarios:

```
LD_LIBRARY_PATH=/path/to/OpenCL-ICD-Loader/build \
OCL_ICD_FILENAMES=/path/to/vendor/libOpenCL.so \
OPENCL_LAYERS=/path/to/OpenCL-Layers/build/object-lifetime/libCLObjectLifetimeLayer.so \
/path/to/application
```

## Tutorial

A more in-depth tutorial can be found here:
[OpenCL-Layers-Tutorial](https://github.com/Kerilk/OpenCL-Layers-Tutorial)
