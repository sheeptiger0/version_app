#pragma once

// Pure-math feature extraction shared by every backend. No OpenCV dependency,
// so this header stays usable even in the mock (model-free) build.

#include <cmath>
#include <vector>

namespace fatigue {

struct Point2f {
  float x = 0.0f;
  float y = 0.0f;
};

// Per-frame features that feed the temporal state decider.
struct FaceFeatures {
  float ear   = 1.0f;   // eye aspect ratio (1.0 = wide open placeholder)
  float mar   = 0.0f;   // mouth aspect ratio
  float yaw   = 0.0f;   // degrees
  float pitch = 0.0f;   // degrees
  float roll  = 0.0f;   // degrees
  bool  hasFace = true;
  // Normalized bounding box [0,1] within the frame.
  float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
};

inline float dist(const Point2f& a, const Point2f& b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

// Eye aspect ratio for a single eye given 6 contour points in order.
inline float eyeEAR(const Point2f* p) {
  const float v = dist(p[1], p[5]) + dist(p[2], p[4]);
  const float h = 2.0f * dist(p[0], p[3]);
  return h > 1e-6f ? v / h : 0.0f;
}

// EAR averaged over both eyes (68-pt landmark layout: left 36..41, right 42..47).
inline float ear(const std::vector<Point2f>& lm) {
  if (lm.size() < 48) return 1.0f;
  Point2f le[6], re[6];
  for (int i = 0; i < 6; ++i) {
    le[i] = lm[36 + i];
    re[i] = lm[42 + i];
  }
  return 0.5f * (eyeEAR(le) + eyeEAR(re));
}

// Mouth aspect ratio (68-pt mouth 48..67), using inner-mouth vertical spans.
inline float mar(const std::vector<Point2f>& lm) {
  if (lm.size() < 68) return 0.0f;
  const float v = dist(lm[62], lm[66]) + dist(lm[63], lm[65]);
  const float h = 2.0f * dist(lm[60], lm[64]);
  return h > 1e-6f ? v / h : 0.0f;
}

} // namespace fatigue
