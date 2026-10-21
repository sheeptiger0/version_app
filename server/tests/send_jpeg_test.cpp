// Sends a real JPEG file to a running fatigue_server (hello + fragmented frame)
// and prints the state JSON reply. Build:
//   g++ -std=c++17 -DASIO_STANDALONE -D_WIN32_WINNT=0x0A00 \
//       -I src -I build/default/_deps/asio-src/asio/include \
//       tests/send_jpeg_test.cpp -o build/send_jpeg_test.exe -lws2_32
#include <asio.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "net/Protocol.h"

using asio::ip::udp;
using namespace DMSAlgo;

static std::vector<std::vector<uint8_t>> fragment(const std::vector<uint8_t>& payload,
                                                  uint32_t frameId) {
  std::vector<std::vector<uint8_t>> out;
  const size_t fragCnt = (payload.size() + kMaxFragmentPayload - 1) / kMaxFragmentPayload;
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

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: send_jpeg_test <file.jpg> <port>\n");
    return 2;
  }
  std::ifstream f(argv[1], std::ios::binary);
  if (!f) {
    std::printf("cannot open %s\n", argv[1]);
    return 2;
  }
  std::vector<uint8_t> payload((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
  const uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));

  asio::io_context io;
  udp::socket sock(io);
  sock.open(udp::v4());
  udp::endpoint server(asio::ip::make_address("127.0.0.1"), port);

  auto hello = packMessage(MsgType::ClientHello, 0, std::string("hello"));
  sock.send_to(asio::buffer(hello), server);

  auto frags = fragment(payload, 1);
  for (const auto& fg : frags) sock.send_to(asio::buffer(fg), server);

  std::vector<uint8_t> buf(4096);
  udp::endpoint from;
  asio::error_code ec;
  const size_t n = sock.receive_from(asio::buffer(buf), from, 0, ec);
  if (ec) {
    std::printf("recv failed: %s\n", ec.message().c_str());
    return 1;
  }
  Header h;
  if (!unpackHeader(buf.data(), n, h) ||
      h.type != static_cast<uint8_t>(MsgType::ServerState)) {
    std::printf("unexpected reply (type=%u)\n", h.type);
    return 1;
  }
  std::printf("reply: %s\n", std::string(buf.begin() + kHeaderSize, buf.begin() + n).c_str());
  return 0;
}
