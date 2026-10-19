#pragma once

// 真实推理管线：人脸检测 -> 关键点 -> EAR/MAR/solvePnP -> StateDecider。
// 与推理后端解耦：通过 IInferenceBackend 跑 ONNX Runtime（OrtBackend）或
// 原生 TensorRT（TrTBackend）。仅在开启真实后端（FATIGUE_ENABLE_ONNX 或
// FATIGUE_ENABLE_TENSORRT）时编译。

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "pipeline/Pipeline.h"
#include "pipeline/StateDecider.h"

namespace fatigue {

class IInferenceBackend;

class ModelPipeline : public IPipeline {
 public:
  explicit ModelPipeline(const PipelineOptions& opts);   //explicit禁止隐式类型转换。

  bool ready() const { return ready_; }

  DetectResult process(const uint8_t* jpeg, size_t len) override;
  std::string backendName() const override { return backendName_; }

 private:
  StateDecider decider_;
  std::unique_ptr<IInferenceBackend> faceDet_;      // 人脸检测后端
  std::unique_ptr<IInferenceBackend> landmarkDet_;  // 关键点后端（可缺失，用 5 点兜底）
  bool hasLandmark_ = false;
  bool ready_ = false;
  std::string backendName_ = "onnx";                // 实际后端名（onnx / tensorrt）

  uint32_t frameId_ = 0;
  double fps_ = 0.0;
  std::chrono::steady_clock::time_point t0_;
  size_t frames_ = 0;
};

} // namespace fatigue
