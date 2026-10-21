#include "inference/OrtBackend.h"

#include "util/Log.h"

namespace DMSAlgo {

OrtBackend::OrtBackend(const std::string& modelPath, const std::string& device) {
  try {
    env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "fatigue_server");
    opts_ = std::make_unique<Ort::SessionOptions>();
    opts_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    opts_->SetIntraOpNumThreads(1);

    if (device == "cuda") {
      // Best effort: if the CUDA provider isn't available, keep CPU.
      try {
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CUDA(*opts_, 0));
        LOG_INFO("ort") << "using CUDA provider";
      } catch (const std::exception& e) {
        LOG_WARN("ort") << "CUDA provider unavailable (" << e.what() << "), using CPU";
      }
    }

    session_ = std::make_unique<Ort::Session>(*env_, modelPath.c_str(), *opts_);

    Ort::AllocatorWithDefaultOptions alloc;
    const size_t nIn = session_->GetInputCount();
    const size_t nOut = session_->GetOutputCount();
    inputNames_.reserve(nIn);
    for (size_t i = 0; i < nIn; ++i) {
      auto name = session_->GetInputNameAllocated(i, alloc);
      inputNames_.emplace_back(name.get());
    }
    outputNames_.reserve(nOut);
    outputShapes_.reserve(nOut);
    for (size_t i = 0; i < nOut; ++i) {
      auto name = session_->GetOutputNameAllocated(i, alloc);
      outputNames_.emplace_back(name.get());
      const auto info = session_->GetOutputTypeInfo(i);
      const auto shapeInfo = info.GetTensorTypeAndShapeInfo();
      outputShapes_.push_back(shapeInfo.GetShape());
    }

    const auto inInfo = session_->GetInputTypeInfo(0);
    inputShape_ = inInfo.GetTensorTypeAndShapeInfo().GetShape();

    ok_ = true;
  } catch (const std::exception& e) {
    LOG_ERROR("ort") << "failed to load '" << modelPath << "': " << e.what();
    ok_ = false;
  }
}

std::vector<std::vector<float>> OrtBackend::run(
    const std::vector<float>& input,
    const std::vector<int64_t>& inputShape,
    std::vector<std::vector<int64_t>>& outputShapes) const {
  std::vector<std::vector<float>> outputs;
  if (!ok_) return outputs;

  Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  std::vector<Ort::Value> inputTensors;
  inputTensors.emplace_back(Ort::Value::CreateTensor<float>(
      memInfo, const_cast<float*>(input.data()), input.size(),
      inputShape.data(), inputShape.size()));

  const std::vector<const char*> inNames{inputNames_[0].c_str()};
  std::vector<const char*> outNames;
  outNames.reserve(outputNames_.size());
  for (const auto& n : outputNames_) outNames.push_back(n.c_str());

  auto outTensors = session_->Run(Ort::RunOptions{nullptr}, inNames.data(),
                                  inputTensors.data(), inputTensors.size(),
                                  outNames.data(), outNames.size());

  outputShapes.clear();
  outputs.reserve(outTensors.size());
  for (auto& t : outTensors) {
    const auto shapeInfo = t.GetTensorTypeAndShapeInfo();
    const auto shape = shapeInfo.GetShape();
    outputShapes.push_back(shape);
    size_t count = 1;
    for (const auto d : shape) count *= static_cast<size_t>(d);
    const float* data = t.GetTensorData<float>();
    outputs.emplace_back(data, data + count);
  }
  return outputs;
}

} // namespace DMSAlgo
