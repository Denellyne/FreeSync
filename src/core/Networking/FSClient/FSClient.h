#pragma once
#include "../FSProtocol.h"
#include "../ThreadPool/ThreadPool.hpp"
#include <contracts>
#include <expected>
#include <print>
#include <unistd.h>

class FSClient final : public FSProtocol {
public:
  FSClient();
  ~FSClient() {
    if (this->_fd != -1)
      close(this->_fd);
    this->_fd = -1;
  }
  virtual void run() override;

private:
  class FSManagerSocket final : public FSProtocol {
  public:
    FSManagerSocket(const std::string &path, const uint64_t nodeId);
    ~FSManagerSocket() {
      if (this->_fd != -1)
        close(this->_fd);
      this->_fd = -1;
    }
    virtual void run() override;

  private:
    int _fd = -1;
    const std::string _path;
    const uint64_t _nodeId = 0;
    bool validationStep();
    bool switchAES();
  };
  int _fd = -1;
  ThreadPool _pool{maxThreads()};
  uint8_t _aesLifetime = 0;
  bool validationStep();
  bool switchAES();
  bool invalidateAES() pre(this->_aes);
  std::expected<bool, std::string> validateCommand(const SSLString &command);
  bool processCommand(const SSLString &command);
  void sendFile(const std::string &path, const uint64_t nodeId);
};
