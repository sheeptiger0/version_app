cmake_minimum_required(VERSION 3.20)
project(fatigue_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ONNX / TensorRT + OpenCV 推理后端默认关闭，保证「无模型/无 GPU」也能立刻编译跑通 mock 链路。
# 需要真实推理时：
#   -DFATIGUE_ENABLE_ONNX=ON      —— ONNX Runtime（需 vcpkg 安装 opencv 与 onnxruntime）
#   -DFATIGUE_ENABLE_TENSORRT=ON  —— 原生 TensorRT（需 MSVC + CUDA Toolkit + TensorRT）
option(FATIGUE_ENABLE_ONNX "Build ONNX Runtime + OpenCV inference backend" OFF)
option(FATIGUE_ENABLE_TENSORRT "Build native TensorRT + OpenCV inference backend" OFF)

# 编译期日志配置：级别与落盘路径在编译时钉死，运行时不再传参。
#   -DFATIGUE_LOG_LEVEL=0|1|2|3   debug/info/warn/error，默认 info(1)
#   -DFATIGUE_LOG_FILE=/tmp/x.log  空 = 只写控制台
set(FATIGUE_LOG_LEVEL 1 CACHE STRING "Compile-time log level: 0=debug 1=info 2=warn 3=error")
set(FATIGUE_LOG_FILE "" CACHE STRING "Compile-time log file path (empty = console only)")

# 自定义 find 模块目录（FindTensorRT.cmake）
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Threads REQUIRED)

include(FetchContent)

# standalone Asio（header-only）
FetchContent_Declare(asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG        asio-1-28-2
  GIT_SHALLOW    TRUE)
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
  FetchContent_Populate(asio)
endif()

# nlohmann/json（header-only）
FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE)
FetchContent_GetProperties(json)
if(NOT json_POPULATED)
  FetchContent_Populate(json)
endif()

add_library(fatigue_core
  src/net/UdpServer.cpp
  src/net/asio_impl.cpp
  src/pipeline/base/StateDecider.cpp
  src/pipeline/mock/MockPipeline.cpp
  src/pipeline/base/Pipeline.cpp
)

target_include_directories(fatigue_core PUBLIC
  ${asio_SOURCE_DIR}/asio/include
  ${json_SOURCE_DIR}/single_include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(fatigue_core PUBLIC
  ASIO_STANDALONE
  ASIO_NO_DEPRECATED
  ASIO_SEPARATE_COMPILATION
  FATIGUE_LOG_LEVEL=${FATIGUE_LOG_LEVEL}
  "FATIGUE_LOG_FILE=\"${FATIGUE_LOG_FILE}\""
)

target_link_libraries(fatigue_core PUBLIC Threads::Threads)

if(WIN32)
  target_compile_definitions(fatigue_core PUBLIC _WIN32_WINNT=0x0A00)
  target_link_libraries(fatigue_core PUBLIC ws2_32 mswsock bcrypt gdi32 user32 windowscodecs ole32)
  target_sources(fatigue_core PRIVATE
    src/video/Jpeg.cpp
    src/video/VideoViewer.cpp
  )
endif()

# 真实推理后端（ONNX 或 TensorRT 任一开启）：都需要 OpenCV 做预处理/后处理。
# 前/后处理与后端无关，集中在 ModelPipeline 基类；OnnxPipeline / TrTPipeline
# 子类只负责创建各自的后端，各占一个文件。
if(FATIGUE_ENABLE_ONNX OR FATIGUE_ENABLE_TENSORRT)
  find_package(OpenCV REQUIRED)

  target_sources(fatigue_core PRIVATE src/pipeline/model/ModelPipeline.cpp)
  target_include_directories(fatigue_core PUBLIC ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(fatigue_core PUBLIC ${OpenCV_LIBS})
endif()

if(FATIGUE_ENABLE_ONNX)
  find_package(onnxruntime REQUIRED)

  target_sources(fatigue_core PRIVATE
    src/inference/OrtBackend.cpp
    src/pipeline/onnx/OnnxPipeline.cpp
  )
  target_compile_definitions(fatigue_core PUBLIC FATIGUE_ENABLE_ONNX=1)
  # vcpkg exposes the shared library as onnxruntime::onnxruntime.
  target_link_libraries(fatigue_core PUBLIC onnxruntime::onnxruntime)
endif()

if(FATIGUE_ENABLE_TENSORRT)
  find_package(CUDAToolkit REQUIRED)
  find_package(TensorRT REQUIRED)

  target_sources(fatigue_core PRIVATE
    src/inference/TrTBackend.cpp
    src/pipeline/trt/TrTPipeline.cpp
  )
  target_compile_definitions(fatigue_core PUBLIC FATIGUE_ENABLE_TENSORRT=1)
  target_link_libraries(fatigue_core PUBLIC CUDA::cudart TensorRT::nvinfer)
endif()

# 实时预览 viewer（Linux/桌面环境，可选）：OpenCV 解码 JPEG + imshow。
# 独立于推理后端——mock 模式下也能用。自动探测：装了 OpenCV 就编译，
# 没装则退回 headless，不破坏「mock 只需 cmake + g++」的零依赖默认。
if(NOT FATIGUE_ENABLE_ONNX AND NOT FATIGUE_ENABLE_TENSORRT)
  find_package(OpenCV QUIET COMPONENTS core imgcodecs highgui)
  if(NOT OpenCV_FOUND)
    # 兜底：Ubuntu 单独装 libopencv-core-dev/imgcodecs-dev/highgui-dev 时没有
    # libopencv-dev 元包，也就没有 OpenCVConfig.cmake。此时头文件在
    # /usr/include/opencv4、库在标准链接路径，直接手写探测。
    find_path(OPENCV4_INCLUDE_DIR opencv2/core.hpp
              PATHS /usr/include/opencv4 /usr/local/include/opencv4)
    find_library(OPENCV4_CORE_LIB      opencv_core)
    find_library(OPENCV4_IMGCODECS_LIB opencv_imgcodecs)
    find_library(OPENCV4_HIGHGUI_LIB   opencv_highgui)
    if(OPENCV4_INCLUDE_DIR AND OPENCV4_CORE_LIB AND
       OPENCV4_IMGCODECS_LIB AND OPENCV4_HIGHGUI_LIB)
      set(OpenCV_FOUND TRUE)
      set(OpenCV_INCLUDE_DIRS ${OPENCV4_INCLUDE_DIR})
      set(OpenCV_LIBS ${OPENCV4_CORE_LIB} ${OPENCV4_IMGCODECS_LIB} ${OPENCV4_HIGHGUI_LIB})
    endif()
  endif()

  if(OpenCV_FOUND)
    message(STATUS "OpenCV found — enabling live preview viewer")
    target_sources(fatigue_core PRIVATE src/video/OpenCvViewer.cpp)
    target_compile_definitions(fatigue_core PUBLIC FATIGUE_ENABLE_VIEWER=1)
    target_include_directories(fatigue_core PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(fatigue_core PUBLIC ${OpenCV_LIBS})
  else()
    message(STATUS "OpenCV not found — building headless (no live preview)")
  endif()
endif()

add_executable(fatigue_server src/main.cpp)
target_link_libraries(fatigue_server PRIVATE fatigue_core)
