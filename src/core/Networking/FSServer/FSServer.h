#pragma once
#include "../FSManager/FSManager.h"
#include "../FSProtocol.h"
#include "../ThreadPool/ThreadPool.hpp"
#include "FSConnection.hpp"
#include <atomic>
#include <cmath>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <print>
#include <unistd.h>

class FSServer {
public:
  FSServer() = delete;
  explicit FSServer(std::atomic_bool &running) noexcept;
  ~FSServer() noexcept {
    std::println("Closing server...");
    if (this->_serverFD != -1)
      close(this->_serverFD);
    this->_serverFD = -1;
  }
  void run() noexcept;
  static void handleConnection(const int fd, const std::atomic_bool &running,
                               FSManager &manager) noexcept;

  operator bool() const noexcept { return !this->_error; }

private:
  class Connection final : public FSConnection {
  public:
    Connection() = delete;
    Connection(const int fd, FSManager &manager) noexcept;
    ~Connection() noexcept {
      if (this->_requestId > 0)
        const bool _ = this->_manager.removeDeadConnection(this->_requestId);
      std::println("Closing connection of sock:{}", this->_fd);
      close(this->_fd);
    }
    void setBusy() noexcept { this->_busy.store(true); }
    [[nodiscard]] bool isBusy() const noexcept { return this->_busy.load(); }
    void run() noexcept override;
    [[nodiscard]] int getFd() const noexcept { return this->_fd; }

  private:
    [[nodiscard]] static std::string
    prettyFileSize(const size_t size) noexcept {
      int o{};
      double mantissa = static_cast<double>(size);
      for (; mantissa >= 1024.; mantissa /= 1024., ++o)
        ;
      return std::to_string(std::ceil(mantissa * 10.) / 10.) + "BKMGTPE"[o];
    }

    bool interpretCommand(const SSLString &command) noexcept;
    bool interpretCommandRSA(const SSLString &command) noexcept;
    bool changeTimeout() {
      struct timeval timeout;
      timeout.tv_sec = 60 * 3;
      timeout.tv_usec = 0;
      if (setsockopt(this->_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                     sizeof(timeout)) != 0) [[unlikely]] {
        std::println("Unable to update timeout value");
        return false;
      }
      if (setsockopt(this->_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                     sizeof(timeout)) != 0) [[unlikely]] {
        std::println("Unable to update timeout value");
        return false;
      }

      int idle = 120;
      int intvl = 30;
      int cnt = 5;

      if (setsockopt(this->_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle,
                     sizeof(idle)) != 0) [[unlikely]] {
        this->_error = true;
        std::println("Unable to update timeout value");
        return false;
      }
      if (setsockopt(this->_fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl,
                     sizeof(intvl)) != 0) [[unlikely]] {
        this->_error = true;
        std::println("Unable to update timeout value");
        return false;
      }
      if (setsockopt(this->_fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt)) !=
          0) [[unlikely]] {
        this->_error = true;
        std::println("Unable to update timeout value");
        return false;
      }
      return true;
    }

    std::atomic_bool _busy = false;
    uint64_t _requestId = 0;
    FSManager &_manager;
  };
  ThreadPool _pool{maxThreads()};
  FSManager _manager;
  std::atomic_bool &_running;
  int _serverFD = -1;
  struct sockaddr_in _sockAddr;
  bool _error = false;
};
