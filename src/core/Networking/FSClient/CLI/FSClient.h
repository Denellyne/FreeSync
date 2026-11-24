#pragma once
#include "../../ThreadPool/ThreadPool.hpp"
#include "../FSClientI.h"
#include <expected>
#include <netinet/in.h>
#include <unistd.h>

class FSClient final : public FSClientI {
public:
  FSClient(const std::string_view ip) noexcept;
  FSClient &operator=(FSClient &&other) {
    std::swap(this->_serverAddr, other._serverAddr);
    std::swap(this->_fd, other._fd);
    std::swap(this->_private, other._private);
    std::swap(this->_public, other._public);

    return *this;
  };
  ~FSClient() noexcept {
    if (this->_fd != -1)
      close(this->_fd);
    this->_fd = -1;
  }
  virtual void run() noexcept override;

  std::expected<bool, std::string_view>
  validateCommand(const SSLString &command) noexcept;

  static const std::string &getIp(const std::string_view ip = "") noexcept {
    static bool initialized = false;
    static std::string IP;
    if (!initialized) {
      IP = ip;

      initialized = true;
    }
    return IP;
  }

private:
  class FSManagerSocket final : public FSClientI {
  public:
    FSManagerSocket(const std::string &file, const ::std::string &fsPath,
                    const uint64_t nodeId);
    ~FSManagerSocket() noexcept {
      if (this->_fd != -1)
        close(this->_fd);
      this->_fd = -1;
    }
    virtual void run() noexcept override;

  private:
    const std::string _file = "", _fsPath = "";
    const uint64_t _nodeId = 0;
  };
  ThreadPool _pool{maxThreads()};
  bool processCommand(const SSLString &command) noexcept;
  void sendFile(const std::string &file, const std::string &fsPath,
                const uint64_t nodeId) const noexcept;
};
