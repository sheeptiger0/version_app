#pragma once

#include <chrono>
#include <cstdint>

#include "pipeline/Pipeline.h"
#include "pipeline/StateDecider.h"

namespace fatigue {

// Model-free backend used to exercise the whole network -> pipeline -> decision
// -> reply loop. Synthesizes a drifting face whose EAR/yaw periodically cross
// the thresholds, so all three states ("awake"/"distracted"/"drowsy") appear.
class MockPipeline : public IPipeline {
 public:
  MockPipeline();

  DetectResult process(const uint8_t* jpeg, size_t len) override;
  std::string backendName() const override { return "mock"; }

 private:
  StateDecider decider_;
  uint32_t frameId_ = 0;
  double fps_ = 0.0;
  std::chrono::steady_clock::time_point t0_;
  size_t frames_ = 0;
};

} // namespace fatigue
