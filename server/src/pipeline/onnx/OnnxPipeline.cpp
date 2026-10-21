#include "pipeline/onnx/OnnxPipeline.h"

#include <memory>

#include "inference/OrtBackend.h"

namespace DMSAlgo {

OnnxPipeline::OnnxPipeline(const PipelineOptions& opts) : ModelPipeline(opts) {
  initialize(opts.modelDir, ".onnx");
}

std::unique_ptr<IInferenceBackend> OnnxPipeline::createBackend(
    const std::string& modelPath) {
  return std::make_unique<OrtBackend>(modelPath, device_);
}

} // namespace DMSAlgo
