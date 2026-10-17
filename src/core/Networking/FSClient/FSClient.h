#pragma once
#include "../FSProtocol.h"
#include "../ThreadPool/ThreadPool.hpp"
#include <expected>
#include <unistd.h>

class FSClient final : public FSProtocol {
public:
  FSClient() noexcept;
  ~FSClient() noexcept {
    if (this->_fd != -1)
      close(this->_fd);
    this->_fd = -1;
  }
  virtual void run() noexcept override;

private:
  class FSManagerSocket final : public FSProtocol {
  public:
    FSManagerSocket(const std::string &path, const uint64_t nodeId);
    ~FSManagerSocket() noexcept {
      if (this->_fd != -1)
        close(this->_fd);
      this->_fd = -1;
    }
    virtual void run() noexcept override;

  private:
    int _fd = -1;
    const std::string _path;
    const uint64_t _nodeId = 0;
    bool validationStep() noexcept;
    bool switchAES() noexcept;
  };
  int _fd = -1;
  ThreadPool _pool{maxThreads()};
  uint8_t _aesLifetime = 0;
  bool validationStep() noexcept;
  bool switchAES() noexcept;
  bool invalidateAES() noexcept;
  std::expected<bool, std::string>
  validateCommand(const SSLString &command) noexcept;
  bool processCommand(const SSLString &command) noexcept;
  void sendFile(const std::string &path, const uint64_t nodeId) noexcept;
};
