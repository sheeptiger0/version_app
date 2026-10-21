// Offline unit test for the wire protocol + fragment reassembly.
// Depends only on header-only code (no asio/OpenCV/ONNX), so it can be built
// anywhere:  g++ -std=c++17 -I src tests/protocol_test.cpp -o protocol_test
//
// Mirrors the Android client's fragmentation scheme (see
// android/.../proto/FrameProtocol.kt) to prove the two ends agree on the wire.

#include <cstdio>
#include <cstdint>
#include <vector>

#include "net/FrameReassembler.h"
#include "net/Protocol.h"

using namespace DMSAlgo;

static int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

// Reproduces the client-side fragmenter (FrameProtocol.fragment in Kotlin).
static std::vector<std::vector<uint8_t>> clientFragment(const std::vector<uint8_t>& payload,
                                                        uint32_t frameId) {
  std::vector<std::vector<uint8_t>> out;
  const size_t fragCnt = (payload.size() + kMaxFragmentPayload - 1) / kMaxFragmentPayload;
  if (fragCnt == 0) return out;
  size_t off = 0;
  uint16_t idx = 0;
  while (off < payload.size()) {
    const size_t n = std::min<size_t>(kMaxFragmentPayload, payload.size() - off);
    std::vector<uint8_t> dg(kHeaderSize + n);
    Header h;
    h.type = static_cast<uint8_t>(MsgType::VideoFragment);
    h.frameId = frameId;
    h.fragIdx = idx;
    h.fragCnt = static_cast<uint16_t>(fragCnt);
    packHeader(h, dg.data());
    std::copy(payload.begin() + off, payload.begin() + off + n, dg.begin() + kHeaderSize);
    out.push_back(std::move(dg));
    off += n;
    ++idx;
  }
  return out;
}

// Feed one client datagram into the reassembler, returning any completed frame.
static bool feed(FrameReassembler& r, const std::vector<uint8_t>& dg,
                 std::vector<uint8_t>& out) {
  Header h;
  if (!unpackHeader(dg.data(), dg.size(), h)) return false;
  return r.add(h, dg.data() + kHeaderSize, dg.size() - kHeaderSize, out);
}

static void testHeaderRoundTrip() {
  Header h;
  h.magic = kMagic;
  h.type = 0x01;
  h.frameId = 0x12345678;
  h.fragIdx = 0xABCD;
  h.fragCnt = 0x0003;

  uint8_t buf[kHeaderSize];
  packHeader(h, buf);

  // Exact big-endian byte layout (must match the Kotlin ByteBuffer encoding).
  CHECK(buf[0] == 0xFA && buf[1] == 0xCE);
  CHECK(buf[2] == 0x01);
  CHECK(buf[3] == 0x12 && buf[4] == 0x34 && buf[5] == 0x56 && buf[6] == 0x78);
  CHECK(buf[7] == 0xAB && buf[8] == 0xCD);
  CHECK(buf[9] == 0x00 && buf[10] == 0x03);

  Header h2;
  CHECK(unpackHeader(buf, kHeaderSize, h2));
  CHECK(h2.magic == kMagic && h2.type == 0x01 && h2.frameId == 0x12345678);
  CHECK(h2.fragIdx == 0xABCD && h2.fragCnt == 0x0003);

  // Bad magic / too short rejected.
  uint8_t bad[kHeaderSize] = {0x00, 0x00};
  CHECK(!unpackHeader(bad, kHeaderSize, h2));
  CHECK(!unpackHeader(buf, kHeaderSize - 1, h2));
}

static void testSingleFragment() {
  std::vector<uint8_t> payload(300, 0x5A);
  auto dgs = clientFragment(payload, 7);
  CHECK(dgs.size() == 1);

  FrameReassembler r;
  std::vector<uint8_t> out;
  CHECK(feed(r, dgs[0], out));       // completes immediately
  CHECK(out == payload);
}

static void testMultiFragmentOutOfOrderAndDuplicate() {
  std::vector<uint8_t> payload(3000);  // -> 3 fragments
  for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i * 7);
  const uint32_t id = 42;
  auto dgs = clientFragment(payload, id);
  CHECK(dgs.size() == 3);

  FrameReassembler r;
  std::vector<uint8_t> out;

  CHECK(!feed(r, dgs[2], out)); // out of order
  CHECK(!feed(r, dgs[0], out));
  CHECK(!feed(r, dgs[0], out)); // duplicate must be ignored, not complete the frame
  CHECK(out.empty());
  CHECK(feed(r, dgs[1], out));  // last fragment completes
  CHECK(out == payload);
}

static void testStaleFrameDropped() {
  FrameReassembler r;
  std::vector<uint8_t> out;
  std::vector<uint8_t> p(100, 0x11);

  // Complete frame 100.
  auto dgs = clientFragment(p, 100);
  for (auto& d : dgs) feed(r, d, out);

  // A fragment for frame 5 is far behind -> dropped (and must not complete).
  auto stale = clientFragment(p, 5);
  std::vector<uint8_t> out2;
  CHECK(!feed(r, stale[0], out2));
  CHECK(out2.empty());
}

static void testNewerFrameClearsOld() {
  FrameReassembler r;
  std::vector<uint8_t> out;
  std::vector<uint8_t> p(3000, 0x22);
  const uint32_t id = 200;
  auto dgs = clientFragment(p, id);
  CHECK(dgs.size() == 3);

  CHECK(!feed(r, dgs[0], out)); // start frame 200

  // A newer frame arrives; the incomplete older frame is discarded.
  auto newer = clientFragment(p, id + 1);
  CHECK(!feed(r, newer[0], out));
  // Fragments of the old frame are now dropped outright.
  CHECK(!feed(r, dgs[1], out));
  CHECK(!feed(r, dgs[2], out));
  // The new frame completes normally once all 3 of its fragments arrive.
  CHECK(!feed(r, newer[1], out));
  CHECK(feed(r, newer[2], out));
  CHECK(out == p);
}

int main() {
  testHeaderRoundTrip();
  testSingleFragment();
  testMultiFragmentOutOfOrderAndDuplicate();
  testStaleFrameDropped();
  testNewerFrameClearsOld();

  if (g_failures == 0) {
    std::printf("ALL PROTOCOL TESTS PASSED\n");
    return 0;
  }
  std::printf("%d CHECK(S) FAILED\n", g_failures);
  return 1;
}
