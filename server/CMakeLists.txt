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
  src/pipeline/StateDecider.cpp
  src/pipeline/MockPipeline.cpp
  src/pipeline/Pipeline.cpp
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

# 真实推理后端（ONNX 或 TensorRT 任一开启）：都需要 OpenCV 做预处理/后处理，
# 且共用同一套 ModelPipeline（前/后处理与后端无关）。
if(FATIGUE_ENABLE_ONNX OR FATIGUE_ENABLE_TENSORRT)
  find_package(OpenCV REQUIRED)

  target_sources(fatigue_core PRIVATE src/pipeline/ModelPipeline.cpp)
  target_include_directories(fatigue_core PUBLIC ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(fatigue_core PUBLIC ${OpenCV_LIBS})
endif()

if(FATIGUE_ENABLE_ONNX)
  find_package(onnxruntime REQUIRED)

  target_sources(fatigue_core PRIVATE src/inference/OrtBackend.cpp)
  target_compile_definitions(fatigue_core PUBLIC FATIGUE_ENABLE_ONNX=1)
  # vcpkg exposes the shared library as onnxruntime::onnxruntime.
  target_link_libraries(fatigue_core PUBLIC onnxruntime::onnxruntime)
endif()

if(FATIGUE_ENABLE_TENSORRT)
  find_package(CUDAToolkit REQUIRED)
  find_package(TensorRT REQUIRED)

  target_sources(fatigue_core PRIVATE src/inference/TrTBackend.cpp)
  target_compile_definitions(fatigue_core PUBLIC FATIGUE_ENABLE_TENSORRT=1)
  target_link_libraries(fatigue_core PUBLIC CUDA::cudart TensorRT::nvinfer)
endif()

add_executable(fatigue_server src/main.cpp)
target_link_libraries(fatigue_server PRIVATE fatigue_core)
