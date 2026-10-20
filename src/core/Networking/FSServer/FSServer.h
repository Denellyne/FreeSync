#pragma once
#include "../FSManager/FSManager.h"
#include "../FSProtocol.h"
#include "../ThreadPool/ThreadPool.hpp"
#include "FSConnection.hpp"
#include <atomic>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <print>
#include <unistd.h>

class FSServer {
public:
  FSServer() = delete;
  FSServer(std::atomic_bool &running) noexcept;
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
    Connection(const int fd, const std::atomic_bool &running,
               FSManager &manager) noexcept;
    ~Connection() noexcept {
      if (this->_requestId > 0)
        const bool _ = this->_manager.removeDeadConnection(this->_requestId);
      std::println("Closing connection of sock:{}", this->_fd);
      close(const_cast<int &>(this->_fd));
    }
    virtual void run() noexcept override;
    operator bool() const noexcept { return !this->_error; }

  private:
    bool interpretCommand(const SSLString &command) noexcept;
    bool interpretCommandRSA(const SSLString &command) noexcept;

    uint64_t _requestId = 0;
    const std::atomic_bool &_running;
    FSManager &_manager;
  };
  ThreadPool _pool{maxThreads()};
  FSManager _manager;
  std::atomic_bool &_running;
  int _serverFD = -1;
  struct sockaddr_in _sockAddr;
  bool _error = false;
};
