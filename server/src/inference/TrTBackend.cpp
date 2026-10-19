#include "inference/TrTBackend.h"

#include <NvInfer.h>
#include <cuda_runtime_api.h>

#include <fstream>
#include <iostream>
#include <vector>

namespace fatigue {

namespace {

// TensorRT 日志适配：告警/错误转发到 stderr，过滤掉 info 刷屏。
class TrTLogger : public nvinfer1::ILogger {
 public:
  void log(Severity severity, const char* msg) noexcept override {
    if (severity <= Severity::kWARNING) std::cerr << "[trt] " << msg << "\n";
  }
};

// Dims 的元素个数。
int64_t dimsVolume(const nvinfer1::Dims& d) {
  int64_t v = 1;
  for (int i = 0; i < d.nbDims; ++i) v *= d.d[i];
  return v;
}

} // namespace

struct TrTBackend::Impl {
  nvinfer1::IRuntime* runtime = nullptr;
  nvinfer1::ICudaEngine* engine = nullptr;
  nvinfer1::IExecutionContext* ctx = nullptr;

  std::string inputName;
  std::vector<std::string> outputNames;
  nvinfer1::Dims inputDims{};
  std::vector<nvinfer1::Dims> outputDims;
  std::vector<size_t> outputCounts;

  void* dInput = nullptr;
  std::vector<void*> dOutputs;
  cudaStream_t stream = nullptr;
  bool ok = false;

  ~Impl() {
    for (void* p : dOutputs) if (p) cudaFree(p);
    if (dInput) cudaFree(dInput);
    if (stream) cudaStreamDestroy(stream);
    // TensorRT 10：context/engine/runtime 都有虚析构，按依赖顺序 delete
    // （context -> engine -> runtime）。若用 TensorRT 8.x，请把 delete 换成 destroy()。
    delete ctx;
    delete engine;
    delete runtime;
  }
};

TrTBackend::TrTBackend(const std::string& enginePath)
    : impl_(std::make_unique<Impl>()) {
  // 1) 从磁盘读序列化好的 engine。
  std::ifstream f(enginePath, std::ios::binary | std::ios::ate);
  if (!f) {
    std::cerr << "[trt] cannot open engine '" << enginePath << "'\n";
    return;
  }
  const std::streamsize size = f.tellg();
  f.seekg(0, std::ios::beg);
  std::vector<char> blob(static_cast<size_t>(size));
  if (!f.read(blob.data(), size)) {
    std::cerr << "[trt] failed to read engine '" << enginePath << "'\n";
    return;
  }

  // 2) 反序列化出 runtime + engine + context。
  TrTLogger logger;
  impl_->runtime = nvinfer1::createInferRuntime(logger);
  if (!impl_->runtime) {
    std::cerr << "[trt] createInferRuntime failed\n";
    return;
  }
  impl_->engine = impl_->runtime->deserializeCudaEngine(blob.data(), blob.size());
  if (!impl_->engine) {
    std::cerr << "[trt] deserializeCudaEngine failed for '" << enginePath << "'\n";
    return;
  }
  impl_->ctx = impl_->engine->createExecutionContext();
  if (!impl_->ctx) {
    std::cerr << "[trt] createExecutionContext failed\n";
    return;
  }

  // 3) 枚举输入/输出张量的名字和形状（用 TRT 10 的 tensor-name API）。
  const int nIO = impl_->engine->getNbIOTensors();
  for (int i = 0; i < nIO; ++i) {
    const char* name = impl_->engine->getIOTensorName(i);
    const auto mode = impl_->engine->getTensorIOMode(name);
    const auto dims = impl_->engine->getTensorShape(name);
    if (mode == nvinfer1::TensorIOMode::kINPUT) {
      impl_->inputName = name;
      impl_->inputDims = dims;
    } else {
      impl_->outputNames.push_back(name);
      impl_->outputDims.push_back(dims);
      impl_->outputCounts.push_back(static_cast<size_t>(dimsVolume(dims)));
    }
  }
  if (impl_->inputName.empty() || impl_->outputNames.empty()) {
    std::cerr << "[trt] engine has no input/output tensors\n";
    return;
  }

  // 4) 分配显存并把张量地址绑定到 context。
  const size_t inBytes = static_cast<size_t>(dimsVolume(impl_->inputDims)) * sizeof(float);
  if (cudaMalloc(&impl_->dInput, inBytes) != cudaSuccess) {
    std::cerr << "[trt] cudaMalloc(input) failed\n";
    return;
  }
  for (size_t i = 0; i < impl_->outputNames.size(); ++i) {
    void* p = nullptr;
    if (cudaMalloc(&p, impl_->outputCounts[i] * sizeof(float)) != cudaSuccess) {
      std::cerr << "[trt] cudaMalloc(output) failed\n";
      return;
    }
    impl_->dOutputs.push_back(p);
  }
  if (cudaStreamCreate(&impl_->stream) != cudaSuccess) {
    std::cerr << "[trt] cudaStreamCreate failed\n";
    return;
  }

  impl_->ctx->setTensorAddress(impl_->inputName.c_str(), impl_->dInput);
  for (size_t i = 0; i < impl_->outputNames.size(); ++i) {
    impl_->ctx->setTensorAddress(impl_->outputNames[i].c_str(), impl_->dOutputs[i]);
  }

  impl_->ok = true;
}

bool TrTBackend::ok() const { return impl_->ok; }

std::vector<int64_t> TrTBackend::inputShape() const {
  std::vector<int64_t> s;
  for (int i = 0; i < impl_->inputDims.nbDims; ++i) s.push_back(impl_->inputDims.d[i]);
  return s;
}

std::vector<std::vector<int64_t>> TrTBackend::outputShapes() const {
  std::vector<std::vector<int64_t>> out;
  for (const auto& d : impl_->outputDims) {
    std::vector<int64_t> s;
    for (int i = 0; i < d.nbDims; ++i) s.push_back(d.d[i]);
    out.push_back(s);
  }
  return out;
}

std::vector<std::vector<float>> TrTBackend::run(
    const std::vector<float>& input,
    const std::vector<int64_t>& inputShape,
    std::vector<std::vector<int64_t>>& outputShapes) const {
  outputShapes.clear();
  (void)inputShape;  // 形状以 engine 的固定维度为准，不使用调用方传入值
  if (!impl_->ok) return {};

  const size_t inCount = static_cast<size_t>(dimsVolume(impl_->inputDims));
  if (input.size() != inCount) {
    std::cerr << "[trt] input size mismatch: got " << input.size()
              << ", expected " << inCount << "\n";
    return {};
  }

  // H2D -> enqueue -> 同步 -> D2H。
  cudaMemcpy(impl_->dInput, input.data(), inCount * sizeof(float),
             cudaMemcpyHostToDevice);
  if (!impl_->ctx->enqueueV3(impl_->stream)) {
    std::cerr << "[trt] enqueueV3 failed\n";
    return {};
  }
  cudaStreamSynchronize(impl_->stream);

  std::vector<std::vector<float>> outputs(impl_->outputNames.size());
  for (size_t i = 0; i < impl_->outputNames.size(); ++i) {
    outputs[i].resize(impl_->outputCounts[i]);
    cudaMemcpy(outputs[i].data(), impl_->dOutputs[i],
               impl_->outputCounts[i] * sizeof(float), cudaMemcpyDeviceToHost);
  }

  outputShapes = outputShapes();
  return outputs;
}

} // namespace fatigue
