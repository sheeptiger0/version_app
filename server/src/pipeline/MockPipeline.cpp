#include "pipeline/MockPipeline.h"

#include <algorithm>
#include <cmath>

namespace fatigue {

MockPipeline::MockPipeline() : t0_(std::chrono::steady_clock::now()) {}

DetectResult MockPipeline::process(const uint8_t* /*jpeg*/, size_t /*len*/) {
  const auto now = std::chrono::steady_clock::now();
  const double t = std::chrono::duration<double>(now - t0_).count();
  ++frames_;
  fps_ = frames_ / std::max(t, 1e-6);

  // 30-second cycle that walks through the states so the phone visibly shows
  // green -> yellow (distracted) -> green -> red (drowsy).
  const double p = std::fmod(t, 30.0);
  FaceFeatures f;
  f.hasFace = true;
  f.ear   = 0.32f;
  f.mar   = 0.12f;
  f.yaw   = 0.0f;
  f.pitch = 0.0f;
  f.roll  = 0.0f;
  if (p >= 10.0 && p < 15.0) {
    f.yaw = 35.0f;        // distracted: head turned away
  } else if (p >= 20.0 && p < 25.0) {
    f.ear = 0.15f;        // drowsy: eyes closed
  }
  f.bx = 0.30f;
  f.by = 0.25f;
  f.bw = 0.40f;
  f.bh = 0.50f;

  DetectResult r;
  r.state = decider_.update(f);
  r.features = f;
  r.frameId = ++frameId_;
  r.fps = fps_;
  return r;
}

} // namespace fatigue
