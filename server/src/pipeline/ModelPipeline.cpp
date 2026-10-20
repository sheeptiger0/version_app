#include "pipeline/ModelPipeline.h"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include "inference/IInferenceBackend.h"

#ifdef FATIGUE_ENABLE_ONNX
#include "inference/OrtBackend.h"
#endif
#ifdef FATIGUE_ENABLE_TENSORRT
#include "inference/TrTBackend.h"
#endif

namespace fatigue {

namespace {

constexpr int kDetInputSize = 640;  // YOLOv8n-face input
constexpr int kLmkInputSize = 112;  // PFLD input

struct FaceDet {
  bool found = false;
  float x = 0, y = 0, w = 0, h = 0;          // normalized [0,1]
  float kpts[5][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}; // normalized
  float score = 0;
};

struct Letterbox {
  cv::Mat img;      // square kDetInputSize x kDetInputSize, RGB
  float scale = 1;  // source -> letterbox
  float padX = 0, padY = 0;
};

// Letterbox-resize a BGR image to a square, returns the transform for mapping
// normalized detection coords back to the original image.
Letterbox letterbox(const cv::Mat& bgr, int size) {
  Letterbox lb;
  const float scale =
      std::min(static_cast<float>(size) / bgr.cols, static_cast<float>(size) / bgr.rows);
  const int newW = static_cast<int>(std::round(bgr.cols * scale));
  const int newH = static_cast<int>(std::round(bgr.rows * scale));
  cv::Mat resized;
  cv::resize(bgr, resized, cv::Size(newW, newH), 0, 0, cv::INTER_LINEAR);

  lb.padX = (size - newW) / 2.0f;
  lb.padY = (size - newH) / 2.0f;

  cv::Mat canvas(size, size, CV_8UC3, cv::Scalar(114, 114, 114));
  resized.copyTo(canvas(cv::Rect(static_cast<int>(std::round(lb.padX)),
                                 static_cast<int>(std::round(lb.padY)), newW, newH)));
  cv::cvtColor(canvas, lb.img, cv::COLOR_BGR2RGB);
  lb.scale = scale;
  return lb;
}

// Letterbox-normalized [0,1] point -> original-image pixel.
void mapToImage(const Letterbox& lb, const cv::Mat& img, float lx, float ly, float& ox, float& oy) {
  ox = (lx * kDetInputSize - lb.padX) / lb.scale;
  oy = (ly * kDetInputSize - lb.padY) / lb.scale;
  ox = std::clamp(ox, 0.0f, static_cast<float>(img.cols - 1));
  oy = std::clamp(oy, 0.0f, static_cast<float>(img.rows - 1));
}

float iou(const FaceDet& a, const FaceDet& b) {
  const float ax1 = a.x, ay1 = a.y, ax2 = a.x + a.w, ay2 = a.y + a.h;
  const float bx1 = b.x, by1 = b.y, bx2 = b.x + b.w, by2 = b.y + b.h;
  const float ix = std::max(0.0f, std::min(ax2, bx2) - std::max(ax1, bx1));
  const float iy = std::max(0.0f, std::min(ay2, by2) - std::max(ay1, by1));
  const float inter = ix * iy;
  const float uni = a.w * a.h + b.w * b.h - inter;
  return uni > 1e-6f ? inter / uni : 0.0f;
}

// 6-point generic 3D face model (dlib-style) for solvePnP with 68 landmarks.
// Indices into the 68-point layout: nose 30, chin 8, left-eye 36,
// right-eye 45, left-mouth 48, right-mouth 54.
//
// 坐标系与 OpenCV 图像一致：X 向右、Y 向下、Z 朝相机（+Z）。
// 原 dlib 模型是 Y 向上、Z 背离相机，直接套 solvePnP 会让正脸测出
// pitch≈±180°（等价于模型绕 X 翻 180°），因此这里把 Y、Z 同时取反。
const std::vector<cv::Point3d>& pose3D68() {
  static const std::vector<cv::Point3d> m = {
      {0.0, 0.0, 0.0},            // nose tip
      {0.0, 330.0, 65.0},         // chin
      {-225.0, -170.0, 135.0},    // left eye corner
      {225.0, -170.0, 135.0},     // right eye corner
      {-150.0, 150.0, 125.0},     // left mouth corner
      {150.0, 150.0, 125.0},      // right mouth corner
  };
  return m;
}

// 5-point model for the YOLO-face landmark fallback (nose, eyes, mouth).
// 同样按 OpenCV 约定（Y 下、Z 朝相机）取反 Y、Z。
const std::vector<cv::Point3d>& pose3D5() {
  static const std::vector<cv::Point3d> m = {
      {0.0, 0.0, 0.0},
      {-225.0, -170.0, 135.0},
      {225.0, -170.0, 135.0},
      {-150.0, 150.0, 125.0},
      {150.0, 150.0, 125.0},
  };
  return m;
}

// Estimate yaw/pitch/roll (degrees) from image points + a 3D model.
void headPose(const std::vector<cv::Point2f>& imgPts,
              const std::vector<cv::Point3d>& objPts,
              const cv::Size& size, float& yaw, float& pitch, float& roll) {
  const double focal = static_cast<double>(size.width);  // heuristic
  cv::Mat cam = (cv::Mat_<double>(3, 3) << focal, 0, size.width / 2.0,
                 0, focal, size.height / 2.0, 0, 0, 1);
  cv::Mat dist = cv::Mat::zeros(4, 1, CV_64F);
  cv::Mat rvec, tvec;
  if (!cv::solvePnP(objPts, imgPts, cam, dist, rvec, tvec, false, cv::SOLVEPNP_ITERATIVE)) {
    return;
  }
  cv::Mat R;
  cv::Rodrigues(rvec, R);  // 3x3 double

  // 旋转矩阵分解为欧拉角（yaw/pitch/roll，单位：度）。
  // 3D 模型已按 OpenCV 约定标定（Y 下、Z 朝相机），正脸应得 yaw≈0、pitch≈0。
  const double sy = std::sqrt(R.at<double>(0, 0) * R.at<double>(0, 0) +
                              R.at<double>(1, 0) * R.at<double>(1, 0));
  if (sy > 1e-6) {
    pitch = static_cast<float>(std::atan2(R.at<double>(2, 1), R.at<double>(2, 2)) * 180.0 / CV_PI);
    yaw   = static_cast<float>(std::atan2(-R.at<double>(2, 0), sy) * 180.0 / CV_PI);
    roll  = static_cast<float>(std::atan2(R.at<double>(1, 0), R.at<double>(0, 0)) * 180.0 / CV_PI);
  }
}

// 根据 opts.backend 创建对应后端：tensorrt -> TrTBackend，否则 -> OrtBackend。
std::unique_ptr<IInferenceBackend> makeBackend(const std::string& modelPath,
                                               const PipelineOptions& opts) {
  if (opts.backend == "tensorrt") {
#ifdef FATIGUE_ENABLE_TENSORRT
    return std::make_unique<TrTBackend>(modelPath);
#else
    (void)modelPath;
    return nullptr;  // 未编译 tensorrt 后端
#endif
  }
#ifdef FATIGUE_ENABLE_ONNX
  return std::make_unique<OrtBackend>(modelPath, opts.device);
#else
  (void)modelPath;
  (void)opts;
  return nullptr;
#endif
}

} // namespace

ModelPipeline::ModelPipeline(const PipelineOptions& opts)
    : t0_(std::chrono::steady_clock::now()), backendName_(opts.backend) {
  // tensorrt 加载 .engine，onnx 加载 .onnx。
  const std::string ext = (opts.backend == "tensorrt") ? ".engine" : ".onnx";

  faceDet_ = makeBackend(opts.modelDir + "/yolov8n-face" + ext, opts);
  if (!faceDet_ || !faceDet_->ok()) {
    std::cerr << "[" << backendName_ << "] face detector failed to load; backend unavailable\n";
    return;
  }
  landmarkDet_ = makeBackend(opts.modelDir + "/pfld" + ext, opts);
  hasLandmark_ = landmarkDet_ && landmarkDet_->ok();
  if (!hasLandmark_) {
    std::cerr << "[" << backendName_ << "] PFLD not found; using 5-point head-pose fallback\n";
  }
  ready_ = true;
}

ModelPipeline::~ModelPipeline() = default;

FaceDet detectFace(IInferenceBackend& det, const cv::Mat& bgr) {
  FaceDet out;
  Letterbox lb = letterbox(bgr, kDetInputSize);

  // BGR->RGB already done; normalize to [0,1] and pack NCHW float.
  std::vector<float> input(3 * kDetInputSize * kDetInputSize);
  for (int c = 0; c < 3; ++c) {
    for (int y = 0; y < kDetInputSize; ++y) {
      for (int x = 0; x < kDetInputSize; ++x) {
        input[c * kDetInputSize * kDetInputSize + y * kDetInputSize + x] =
            lb.img.at<cv::Vec3b>(y, x)[c] / 255.0f;
      }
    }
  }

  std::vector<std::vector<int64_t>> outShapes;
  const auto outputs = det.run(input, {1, 3, kDetInputSize, kDetInputSize}, outShapes);
  if (outputs.empty() || outShapes.empty() || outShapes[0].size() != 3) return out;

  const auto& sh = outShapes[0];  // {1, N, D} or {1, D, N}
  const int64_t a = sh[1], b = sh[2];
  const bool transposed = (a <= 16 && b > 16);
  const int64_t num  = transposed ? b : a;
  const int64_t feat = transposed ? a : b;
  // 至少需要 cx,cy,w,h,conf 五个字段；关键点（15 特征）是可选的——
  // 68 点关键点由 PFLD 提供，检测器只需给出人脸框即可。
  if (feat < 5) return out;

  const float* data = outputs[0].data();
  std::vector<FaceDet> cands;

  for (int64_t i = 0; i < num; ++i) {
    float row[16] = {};
    for (int64_t j = 0; j < feat && j < 16; ++j) {
      row[j] = transposed ? data[j * num + i] : data[i * feat + j];
    }
    const float conf = row[4];
    if (conf < 0.25f) continue;

    const float cx = row[0], cy = row[1], w = row[2], h = row[3];
    // Output may be input-pixel coords or already normalized; detect and scale.
    const float s = (std::abs(cx) > 2.0f || std::abs(cy) > 2.0f) ? 1.0f / kDetInputSize : 1.0f;
    const float x1 = (cx - w / 2) * s, y1 = (cy - h / 2) * s;
    const float x2 = (cx + w / 2) * s, y2 = (cy + h / 2) * s;

    FaceDet c;
    c.score = conf;
    c.x = std::clamp(x1, 0.0f, 1.0f);
    c.y = std::clamp(y1, 0.0f, 1.0f);
    c.w = std::clamp(x2, 0.0f, 1.0f) - c.x;
    c.h = std::clamp(y2, 0.0f, 1.0f) - c.y;
    if (feat >= 15) {  // 仅当检测器输出带 5 个关键点（15 特征）时才解析
      for (int k = 0; k < 5; ++k) {
        c.kpts[k][0] = std::clamp(row[5 + 2 * k] * s, 0.0f, 1.0f);
        c.kpts[k][1] = std::clamp(row[5 + 2 * k + 1] * s, 0.0f, 1.0f);
      }
    }
    cands.push_back(c);
  }

  // Sort by confidence and greedy NMS.
  std::sort(cands.begin(), cands.end(),
            [](const FaceDet& l, const FaceDet& r) { return l.score > r.score; });
  std::vector<FaceDet> kept;
  for (const auto& c : cands) {
    bool overlapped = false;
    for (const auto& k : kept) {
      if (iou(c, k) > 0.5f) { overlapped = true; break; }
    }
    if (!overlapped) kept.push_back(c);
    if (kept.size() >= 1) break; // MVP: keep the single largest-confidence face
  }
  if (!kept.empty()) {
    out = kept.front();
    out.found = true;
    // Map letterbox-normalized box/kpts back to original-image pixel coords.
    float ox, oy;
    mapToImage(lb, bgr, out.x, out.y, ox, oy);
    const float x1 = ox, y1 = oy;
    mapToImage(lb, bgr, out.x + out.w, out.y + out.h, ox, oy);
    out.x = x1 / bgr.cols;
    out.y = y1 / bgr.rows;
    out.w = (ox - x1) / bgr.cols;
    out.h = (oy - y1) / bgr.rows;
    for (int k = 0; k < 5; ++k) {
      mapToImage(lb, bgr, out.kpts[k][0], out.kpts[k][1], ox, oy);
      out.kpts[k][0] = ox / bgr.cols;
      out.kpts[k][1] = oy / bgr.rows;
    }
  }
  return out;
}

std::vector<Point2f> detectLandmarks(IInferenceBackend& lmk, const cv::Mat& bgr, const FaceDet& face) {
  std::vector<Point2f> result;
  // Crop the face with margin and resize to the PFLD input size.
  const int x1 = std::max(0, static_cast<int>(face.x * bgr.cols));
  const int y1 = std::max(0, static_cast<int>(face.y * bgr.rows));
  const int x2 = std::min(bgr.cols, static_cast<int>((face.x + face.w) * bgr.cols));
  const int y2 = std::min(bgr.rows, static_cast<int>((face.y + face.h) * bgr.rows));
  if (x2 <= x1 || y2 <= y1) return result;

  cv::Mat crop = bgr(cv::Rect(x1, y1, x2 - x1, y2 - y1)).clone();
  cv::Mat rgb;
  cv::cvtColor(crop, rgb, cv::COLOR_BGR2RGB);
  cv::Mat resized;
  cv::resize(rgb, resized, cv::Size(kLmkInputSize, kLmkInputSize), 0, 0, cv::INTER_LINEAR);

  std::vector<float> input(3 * kLmkInputSize * kLmkInputSize);
  for (int c = 0; c < 3; ++c)
    for (int y = 0; y < kLmkInputSize; ++y)
      for (int x = 0; x < kLmkInputSize; ++x)
        input[c * kLmkInputSize * kLmkInputSize + y * kLmkInputSize + x] =
            resized.at<cv::Vec3b>(y, x)[c] / 255.0f;

  std::vector<std::vector<int64_t>> outShapes;
  const auto outputs = lmk.run(input, {1, 3, kLmkInputSize, kLmkInputSize}, outShapes);
  if (outputs.empty()) return result;

  const float* data = outputs[0].data();
  size_t count = 1;
  for (const auto d : outShapes[0]) count *= static_cast<size_t>(d);
  const size_t nPts = count / 2;
  if (nPts < 5) return result;

  for (size_t i = 0; i < nPts; ++i) {
    float lx = data[2 * i];
    float ly = data[2 * i + 1];
    // Landmark output is expected in [0, kLmkInputSize] pixel space.
    if (std::abs(lx) <= 2.0f && std::abs(ly) <= 2.0f) { lx *= kLmkInputSize; ly *= kLmkInputSize; }
    const float ox = x1 + (lx / kLmkInputSize) * (x2 - x1);
    const float oy = y1 + (ly / kLmkInputSize) * (y2 - y1);
    result.push_back({ox, oy});
  }
  return result;
}

DetectResult ModelPipeline::process(const uint8_t* jpeg, size_t len) {
  const auto now = std::chrono::steady_clock::now();
  const double t = std::chrono::duration<double>(now - t0_).count();
  ++frames_;
  fps_ = frames_ / std::max(t, 1e-6);

  DetectResult r;
  r.frameId = ++frameId_;
  r.fps = fps_;

  cv::Mat img = cv::imdecode(cv::Mat(1, static_cast<int>(len), CV_8UC1,
                                     const_cast<uint8_t*>(jpeg)),
                             cv::IMREAD_COLOR);
  if (img.empty()) {
    r.state = "none";
    return r;
  }

  FaceFeatures f;
  f.hasFace = false;

  const FaceDet face = detectFace(*faceDet_, img);
  if (!face.found) {
    r.state = decider_.update(f);
    r.features = f;
    return r;
  }

  f.hasFace = true;
  f.bx = face.x;
  f.by = face.y;
  f.bw = face.w;
  f.bh = face.h;

  std::vector<Point2f> lm68;
  if (hasLandmark_) lm68 = detectLandmarks(*landmarkDet_, img, face);

  std::vector<cv::Point2f> imgPts;
  if (lm68.size() >= 68) {
    f.ear = ear(lm68);
    f.mar = mar(lm68);
    // 6-point model over the canonical 68-pt indices.
    static const int idx[6] = {30, 8, 36, 45, 48, 54};
    for (int k = 0; k < 6; ++k) imgPts.emplace_back(lm68[idx[k]].x, lm68[idx[k]].y);
    headPose(imgPts, pose3D68(), img.size(), f.yaw, f.pitch, f.roll);
  } else {
    f.ear = 1.0f;  // placeholder: no reliable eye contour
    f.mar = 0.0f;
    for (int k = 0; k < 5; ++k)
      imgPts.emplace_back(face.kpts[k][0] * img.cols, face.kpts[k][1] * img.rows);
    headPose(imgPts, pose3D5(), img.size(), f.yaw, f.pitch, f.roll);
  }

  r.state = decider_.update(f);
  r.features = f;
  return r;
}

} // namespace fatigue
