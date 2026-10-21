#pragma once

// 真实推理管线基类：人脸检测 -> 关键点 -> EAR/MAR/solvePnP -> StateDecider。
// 前/后处理与推理后端解耦：通过 IInferenceBackend 跑 ONNX Runtime 或
// 原生 TensorRT。具体后端由子类通过 createBackend() 工厂创建，
// onnx（OnnxPipeline）与 tensorrt（TrTPipeline）各占一个文件。
// 仅在开启真实后端（FATIGUE_ENABLE_ONNX 或 FATIGUE_ENABLE_TENSORRT）时编译。

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "pipeline/base/Pipeline.h"
#include "pipeline/base/StateDecider.h"

namespace DMSAlgo {

class IInferenceBackend;

class ModelPipeline : public IPipeline {
 public:
  ~ModelPipeline() override;  // 定义在 .cpp：IInferenceBackend 是不完整类型，析构器不能在头文件内联

  bool ready() const { return ready_; }

  DetectResult process(const uint8_t* jpeg, size_t len) override;
  std::string backendName() const override { return backendName_; }

 protected:
  explicit ModelPipeline(const PipelineOptions& opts);

  // 子类构造后调用：按 ext（".onnx" / ".engine"）加载检测器与关键点模型。
  bool initialize(const std::string& modelDir, const std::string& ext);

  // 子类实现：按后端类型创建推理后端（OrtBackend / TrTBackend）。
  virtual std::unique_ptr<IInferenceBackend> createBackend(
      const std::string& modelPath) = 0;

  std::string device_;  // onnx 子类用；tensorrt 子类忽略

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

} // namespace DMSAlgo
