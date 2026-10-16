#pragma once

#include <cstddef>
#include <deque>
#include <string>

#include "pipeline/Features.h"

namespace fatigue {

struct StateConfig {
  float  earThreshold   = 0.20f; // below this an eye counts as "closed"
  float  earLowFrac     = 0.60f; // drowsy when this fraction of the window is closed
  float  marThreshold   = 0.60f; // yawn / mouth-open threshold
  float  yawThreshold   = 25.0f; // degrees
  float  pitchThreshold = 20.0f; // degrees
  size_t windowFrames   = 30;    // ~2 s at 15 fps
  size_t persistFrames  = 15;    // ~1 s of persistence to avoid flicker
};

// Sliding-window temporal filter + fatigue/distraction decision.
class StateDecider {
 public:
  explicit StateDecider(const StateConfig& cfg = StateConfig{});

  // Incorporate one feature sample; returns the current (possibly unchanged)
  // state: "none" | "awake" | "drowsy" | "distracted".
  std::string update(const FaceFeatures& f);
  void reset();

 private:
  StateConfig cfg_;
  std::deque<FaceFeatures> window_;
  std::string state_ = "awake";
  std::string pending_ = "awake";
  size_t pendingCount_ = 0;
};

} // namespace fatigue
