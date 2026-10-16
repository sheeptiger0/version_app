#pragma once

// Wire protocol shared by the Android client and the C++ server.
//
// Every UDP datagram starts with an 11-byte header:
//   [magic 2B = 0xFA 0xCE][type 1B][frame_id 4B BE][frag_idx 2B BE][frag_cnt 2B BE]
//
// A single JPEG frame is split into fragments of at most kMaxFragmentPayload
// bytes each. `frag_cnt` fragments with the same `frame_id` are reassembled in
// order. Server->client state replies are a single (unfragmented) JSON message.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fatigue {

constexpr uint16_t kMagic = 0xFACE;

enum class MsgType : uint8_t {
  VideoFragment   = 0x01,
  ClientHello     = 0x02,
  ServerState     = 0x03,
  AnnotatedFrame  = 0x04, // reserved
};

constexpr size_t kHeaderSize         = 2 + 1 + 4 + 2 + 2; // 11 bytes
constexpr size_t kMaxFragmentPayload = 1200;
constexpr size_t kMaxDatagramSize    = kHeaderSize + kMaxFragmentPayload;

struct Header {
  uint16_t magic   = kMagic;
  uint8_t  type    = 0;
  uint32_t frameId = 0;
  uint16_t fragIdx = 0;
  uint16_t fragCnt = 0;
};

inline uint16_t read16(const uint8_t* p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}
inline uint32_t read32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
inline void write16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v & 0xFF);
}
inline void write32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[3] = static_cast<uint8_t>(v & 0xFF);
}

inline void packHeader(const Header& h, uint8_t* out) {
  write16(out, h.magic);
  out[2] = h.type;
  write32(out + 3, h.frameId);
  write16(out + 7, h.fragIdx);
  write16(out + 9, h.fragCnt);
}

inline bool unpackHeader(const uint8_t* in, size_t n, Header& h) {
  if (n < kHeaderSize) return false;
  h.magic = read16(in);
  if (h.magic != kMagic) return false;
  h.type    = in[2];
  h.frameId = read32(in + 3);
  h.fragIdx = read16(in + 7);
  h.fragCnt = read16(in + 9);
  return true;
}

inline std::vector<uint8_t> packMessage(MsgType type, uint32_t frameId,
                                        const uint8_t* payload, size_t n) {
  std::vector<uint8_t> out(kHeaderSize + n);
  Header h;
  h.type    = static_cast<uint8_t>(type);
  h.frameId = frameId;
  h.fragIdx = 0;
  h.fragCnt = 1;
  packHeader(h, out.data());
  std::copy(payload, payload + n, out.begin() + static_cast<std::ptrdiff_t>(kHeaderSize));
  return out;
}

inline std::vector<uint8_t> packMessage(MsgType type, uint32_t frameId,
                                        const std::string& payload) {
  return packMessage(type, frameId,
                     reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
}

} // namespace fatigue
