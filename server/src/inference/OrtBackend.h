#pragma once

// RAII wrapper around an ONNX Runtime session. Only compiled when
// FATIGUE_ENABLE_ONNX is ON.

#include <memory>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

#include "inference/IInferenceBackend.h"

namespace DMSAlgo {

class OrtBackend : public IInferenceBackend {
 public:
  OrtBackend(const std::string& modelPath, const std::string& device = "cpu");

  bool ok() const override { return ok_; }

  // Runs one float input tensor of shape `inputShape` (e.g. {1,3,H,W}) and
  // returns each output as flat float data plus its shape.
  std::vector<std::vector<float>> run(
      const std::vector<float>& input,
      const std::vector<int64_t>& inputShape,
      std::vector<std::vector<int64_t>>& outputShapes) const override;

  std::vector<int64_t> inputShape() const override { return inputShape_; }
  std::vector<std::vector<int64_t>> outputShapes() const override { return outputShapes_; }

 private:
  std::unique_ptr<Ort::Env> env_;
  std::unique_ptr<Ort::SessionOptions> opts_;
  std::unique_ptr<Ort::Session> session_;
  std::vector<std::string> inputNames_;
  std::vector<std::string> outputNames_;
  std::vector<int64_t> inputShape_;
  std::vector<std::vector<int64_t>> outputShapes_;
  bool ok_ = false;
};

} // namespace DMSAlgo
