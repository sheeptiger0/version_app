#pragma once

// Header-only fragment reassembly. Depends only on Protocol.h, so the core
// wire logic can be compiled and unit-tested without asio/OpenCV/ONNX.

#include <cstdint>
#include <map>
#include <vector>

#include "net/Protocol.h"

namespace DMSAlgo {

// Reassembles fragmented video frames keyed by frame_id.
// Handles out-of-order and duplicate fragments idempotently, and drops stale
// frames that are far behind the latest seen id.
class FrameReassembler {
 public:
  // Returns true and fills `out` exactly when a frame becomes complete.
  bool add(const Header& h, const uint8_t* payload, size_t n,
           std::vector<uint8_t>& out) {
    const uint32_t id = h.frameId;
    if (h.fragCnt == 0) return false;

    // A single client streams frames with monotonically increasing ids: any
    // fragment of an older frame is stale and dropped outright. This keeps the
    // reassembler leak-free (only one in-flight frame at a time).
    if (hasLatest_ && id < latest_) {
      return false;
    }

    // A newer frame starts: discard any incomplete older frame.
    if (!hasLatest_ || id > latest_) {
      slots_.clear();
      latest_ = id;
      hasLatest_ = true;
    }

    Slot& slot = slots_[id];
    if (slot.total == 0) {
      slot.total = h.fragCnt;
      slot.present.assign(h.fragCnt, false);
      slot.parts.resize(h.fragCnt);
    }
    if (h.fragIdx >= slot.total || slot.present[h.fragIdx]) {
      return false; // out of range or duplicate
    }

    slot.present[h.fragIdx] = true;
    slot.parts[h.fragIdx].assign(payload, payload + n);
    slot.received += 1;

    if (slot.received != slot.total) return false;

    size_t totalSize = 0;
    for (const auto& p : slot.parts) totalSize += p.size();
    out.clear();
    out.reserve(totalSize);
    for (const auto& p : slot.parts) out.insert(out.end(), p.begin(), p.end());
    slots_.erase(id);
    return true;
  }

  void reset() {
    slots_.clear();
    hasLatest_ = false;
    latest_ = 0;
  }

 private:
  struct Slot {
    uint16_t total = 0;
    size_t received = 0;
    std::vector<bool> present;
    std::vector<std::vector<uint8_t>> parts;
  };

  uint32_t latest_ = 0;
  bool hasLatest_ = false;
  std::map<uint32_t, Slot> slots_;
};

} // namespace DMSAlgo
