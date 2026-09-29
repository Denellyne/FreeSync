#pragma once
#include "../FSManager/FSManager.h"
#include "../FSProtocol.h"
#include "../ThreadPool/ThreadPool.hpp"
#include <atomic>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <print>
#include <unistd.h>

class FSServer {
public:
  FSServer() = delete;
  FSServer(std::atomic_bool &running);
  ~FSServer() {
    std::println("Closing server...");
    if (this->_serverFD != -1)
      close(this->_serverFD);
    this->_serverFD = -1;
  }
  void run();
  static void handleConnection(const int fd, const std::atomic_bool &running,
                               FSManager &manager);

private:
  class Connection final : public FSProtocol {
  public:
    Connection() = delete;
    Connection(const int fd, const std::atomic_bool &running, bool &valid,
               FSManager &manager);
    ~Connection() {
      if (this->_requestId > 0)
        this->_manager.removeDeadConnection(this->_requestId);
      std::println("Closing connection of sock:{}", this->_fd);
      close(const_cast<int &>(this->_fd));
    }
    virtual void run() override;

  private:
    // std::string generateRandomString();
    bool handleValidation();
    bool handleAES();
    bool interpretCommand(const SSLString &command);
    bool checkIfAuthorized(const std::vector<unsigned char> &key);

    int _fd = -1;
    uintptr_t _requestId = 0;
    const std::atomic_bool &_running;
    FSManager &_manager;
  };
  ThreadPool _pool{maxThreads()};
  FSManager _manager;
  std::atomic_bool &_running;
  int _serverFD = -1;
  struct sockaddr_in _sockAddr;
};
