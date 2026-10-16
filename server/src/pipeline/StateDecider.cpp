#include "pipeline/StateDecider.h"

#include <cmath>

namespace fatigue {

StateDecider::StateDecider(const StateConfig& cfg) : cfg_(cfg) {}

void StateDecider::reset() {
  window_.clear();
  state_ = "awake";
  pending_ = "awake";
  pendingCount_ = 0;
}

std::string StateDecider::update(const FaceFeatures& f) {
  window_.push_back(f);
  if (window_.size() > cfg_.windowFrames) window_.pop_front();

  // Raw instantaneous state derived from the whole window.
  std::string raw = "awake";
  if (window_.empty() || !f.hasFace) {
    raw = "none";
  } else {
    size_t lowEar = 0, total = 0;
    bool marHigh = false, yawHigh = false, pitchHigh = false;
    for (const auto& s : window_) {
      if (s.ear < cfg_.earThreshold) ++lowEar;
      if (s.mar > cfg_.marThreshold) marHigh = true;
      if (std::abs(s.yaw) > cfg_.yawThreshold) yawHigh = true;
      if (std::abs(s.pitch) > cfg_.pitchThreshold) pitchHigh = true;
      ++total;
    }
    const double earFrac = total ? static_cast<double>(lowEar) / total : 0.0;

    if (earFrac >= cfg_.earLowFrac || marHigh) {
      raw = "drowsy";
    } else if (yawHigh || pitchHigh) {
      raw = "distracted";
    }
  }

  // Hysteresis: a candidate state must persist for persistFrames before it
  // becomes the reported state.
  if (raw != pending_) {
    pending_ = raw;
    pendingCount_ = 1;
  } else if (pending_ != state_ && ++pendingCount_ >= cfg_.persistFrames) {
    state_ = pending_;
  }

  return state_;
}

} // namespace fatigue
