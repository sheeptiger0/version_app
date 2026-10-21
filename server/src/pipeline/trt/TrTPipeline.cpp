#include "pipeline/trt/TrTPipeline.h"

#include <memory>

#include "inference/TrTBackend.h"

namespace DMSAlgo {

TrTPipeline::TrTPipeline(const PipelineOptions& opts) : ModelPipeline(opts) {
  initialize(opts.modelDir, ".engine");
}

std::unique_ptr<IInferenceBackend> TrTPipeline::createBackend(
    const std::string& modelPath) {
  return std::make_unique<TrTBackend>(modelPath);
}

} // namespace DMSAlgo
