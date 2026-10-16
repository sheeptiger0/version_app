#pragma once

// Real inference pipeline: YOLOv8n-face detection -> PFLD landmarks ->
// EAR/MAR/solvePnP features -> StateDecider. Only compiled when
// FATIGUE_ENABLE_ONNX is ON.

#include <chrono>
#include <cstdint>
#include <memory>

#include "pipeline/Pipeline.h"
#include "pipeline/StateDecider.h"

namespace fatigue {

class OrtBackend;

class OnnxPipeline : public IPipeline {
 public:
  explicit OnnxPipeline(const PipelineOptions& opts);

  bool ready() const { return ready_; }

  DetectResult process(const uint8_t* jpeg, size_t len) override;
  std::string backendName() const override { return "onnx"; }

 private:
  StateDecider decider_;
  std::unique_ptr<OrtBackend> faceDet_;
  std::unique_ptr<OrtBackend> landmarkDet_;
  bool hasLandmark_ = false;
  bool ready_ = false;

  uint32_t frameId_ = 0;
  double fps_ = 0.0;
  std::chrono::steady_clock::time_point t0_;
  size_t frames_ = 0;
};

} // namespace fatigue
