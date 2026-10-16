#include "net/UdpServer.h"

namespace fatigue {

using asio::ip::udp;

UdpServer::UdpServer(asio::io_context& io, uint16_t port) : socket_(io) {
  udp::endpoint ep(udp::v4(), port);
  asio::error_code ec;
  socket_.open(udp::v4(), ec);
  if (!ec) socket_.bind(ep, ec);
  bound_ = !ec;
}

UdpServer::~UdpServer() { stop(); }

void UdpServer::start(FrameCallback cb) {
  onFrame_ = std::move(cb);
  running_ = true;
  thread_ = std::thread([this] { recvLoop(); });
}

void UdpServer::stop() {
  running_ = false;
  asio::error_code ec;
  socket_.close(ec);
  if (thread_.joinable()) thread_.join();
}

void UdpServer::recvLoop() {
  std::vector<uint8_t> buf(kMaxDatagramSize);
  while (running_) {
    udp::endpoint sender;
    asio::error_code ec;
    const size_t n = socket_.receive_from(asio::buffer(buf), sender, 0, ec);
    if (ec) {
      if (running_) continue;
      break;
    }

    Header h;
    if (!unpackHeader(buf.data(), n, h)) continue;

    if (h.type == static_cast<uint8_t>(MsgType::ClientHello)) {
      std::lock_guard<std::mutex> lk(mtx_);
      lastClient_ = sender;
      continue;
    }
    if (h.type != static_cast<uint8_t>(MsgType::VideoFragment)) continue;

    std::vector<uint8_t> frame;
    bool done = false;
    {
      std::lock_guard<std::mutex> lk(mtx_);
      lastClient_ = sender;
      done = reassembler_.add(h, buf.data() + kHeaderSize, n - kHeaderSize, frame);
    }
    if (done && onFrame_) onFrame_(frame, sender);
  }
}

void UdpServer::sendTo(const udp::endpoint& to, const std::vector<uint8_t>& datagram) {
  std::lock_guard<std::mutex> lk(mtx_);
  asio::error_code ec;
  socket_.send_to(asio::buffer(datagram), to, 0, ec);
}

udp::endpoint UdpServer::lastClient() const {
  std::lock_guard<std::mutex> lk(mtx_);
  return lastClient_;
}

} // namespace fatigue
