# 定位 NVIDIA TensorRT（nvinfer）。定义 imported target TensorRT::nvinfer，
# 并设置 TensorRT_FOUND。
#
# 用法：把 TensorRT_ROOT 环境变量（或 CMake 变量）指向解压目录，
# 例如 C:/TensorRT-10.0.1.6；不设则回退到默认安装路径。

set(_trt_hints
  "$ENV{TensorRT_ROOT}"
  "${TensorRT_ROOT}"
  "C:/Program Files/NVIDIA GPU Computing Toolkit/TensorRT"
  "C:/TensorRT"
  "/usr/local/TensorRT"
  "/opt/TensorRT"
  "/usr/local/cuda"
  "/usr/local"
  "/usr")

find_path(TensorRT_INCLUDE_DIR NvInfer.h
  HINTS ${_trt_hints}
  PATH_SUFFIXES include include/x86_64-linux-gnu)

find_library(TensorRT_LIBRARY nvinfer
  HINTS ${_trt_hints}
  PATH_SUFFIXES lib lib/x64 lib64 lib/x86_64-linux-gnu)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TensorRT
  REQUIRED_VARS TensorRT_INCLUDE_DIR TensorRT_LIBRARY)

if(TensorRT_FOUND AND NOT TARGET TensorRT::nvinfer)
  add_library(TensorRT::nvinfer UNKNOWN IMPORTED)
  set_target_properties(TensorRT::nvinfer PROPERTIES
    IMPORTED_LOCATION "${TensorRT_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${TensorRT_INCLUDE_DIR}")
endif()
