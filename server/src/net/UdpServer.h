#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <asio.hpp>

#include "net/FrameReassembler.h"
#include "net/Protocol.h"

namespace fatigue {

// Binds a UDP socket and runs a blocking receive loop on its own thread.
class UdpServer {
 public:
  using FrameCallback = std::function<void(
      const std::vector<uint8_t>& jpeg, const asio::ip::udp::endpoint& from)>;

  UdpServer(asio::io_context& io, uint16_t port);
  ~UdpServer();

  UdpServer(const UdpServer&) = delete;
  UdpServer& operator=(const UdpServer&) = delete;

  bool bound() const { return bound_; }

  void start(FrameCallback cb);
  void stop();

  // Send one (already packed) datagram. Thread-safe w.r.t. other sends.
  void sendTo(const asio::ip::udp::endpoint& to, const std::vector<uint8_t>& datagram);
  asio::ip::udp::endpoint lastClient() const;

 private:
  void recvLoop();

  asio::ip::udp::socket socket_;
  bool bound_ = false;

  FrameReassembler reassembler_;
  FrameCallback onFrame_;
  std::thread thread_;
  std::atomic<bool> running_{false};

  mutable std::mutex mtx_;
  asio::ip::udp::endpoint lastClient_;
};

} // namespace fatigue
