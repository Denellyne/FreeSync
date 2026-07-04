#pragma once
#include "../FSProtocol.h"
#include "../ThreadPool/ThreadPool.hpp"
#include <atomic>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <print>
#include <unistd.h>
#define PKEY_PATH "certs/priv.pem"

class FSServer {
public:
  FSServer() = delete;
  FSServer(std::atomic_bool &running);
  ~FSServer() {
    std::println("Closing server");
    if (this->_serverFD != -1)
      close(this->_serverFD);
    this->_serverFD = -1;
  }
  void run();
  static void handleConnection(const int fd, const std::atomic_bool &running);

private:
  class Connection final : public FSProtocol {
  public:
    Connection() = delete;
    Connection(const int fd, const std::atomic_bool &running, bool &valid);
    ~Connection() {
      std::println("Closing connection of sock:{}", this->_fd);
      close(const_cast<int &>(this->_fd));
    }
    virtual void run() override;

  private:
    int _fd = -1;
    const std::atomic_bool &_running;
  };
  ThreadPool _pool{maxThreads()};
  std::atomic_bool &_running;
  int _serverFD = -1;
  struct sockaddr_in _sockAddr;
};
