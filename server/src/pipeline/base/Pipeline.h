#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "pipeline/base/Features.h"

namespace DMSAlgo {

struct DetectResult {
  std::string state = "awake";
  FaceFeatures features;
  uint32_t frameId = 0;
  double fps = 0.0;
};

// Pluggable inference pipeline: every backend turns a JPEG frame into a
// DetectResult. The mock backend lets the full network loop run without models.
class IPipeline {
 public:
  virtual ~IPipeline() = default;
  virtual DetectResult process(const uint8_t* jpeg, size_t len) = 0;
  virtual std::string backendName() const = 0;
};

struct PipelineOptions {
  std::string backend  = "mock";   // "mock" | "onnx" | "tensorrt"
  std::string device   = "cpu";    // "cpu" | "cuda"
  std::string modelDir = "models";
};

std::unique_ptr<IPipeline> makePipeline(const PipelineOptions& opts);

} // namespace DMSAlgo
