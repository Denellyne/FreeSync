#pragma once
#include "../FSProtocol.h"
#include "../ThreadPool/ThreadPool.hpp"
#include <expected>
#include <netinet/in.h>
#include <unistd.h>

class FSClient final : public FSProtocol {
public:
  FSClient(const std::string_view ip) noexcept;
  FSClient() = delete;
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

  bool connect() noexcept;
  std::expected<bool, std::string_view>
  validateCommand(const SSLString &command) noexcept;
  std::optional<SSLString> issueCommandRead(const SSLString &command) noexcept;
  bool issueCommand(const SSLString &command) noexcept;
  bool downloadFile(const std::string_view outPath);

  void disconnect() {
    if (this->_fd >= 0)
      close(this->_fd);
    this->_fd = -1;
  }

private:
  class FSManagerSocket final : public FSProtocol {
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
    int _fd = -1;
    const std::string _file = "", _fsPath = "";
    const uint64_t _nodeId = 0;
    bool validationStep() noexcept;
    bool switchAES() noexcept;
  };
  int _fd = -1;
  struct sockaddr_in _serverAddr;
  ThreadPool _pool{maxThreads()};
  uint8_t _aesLifetime = 0;
  bool validationStep() noexcept;
  bool switchAES() noexcept;
  bool processCommand(const SSLString &command) noexcept;
  bool invalidateAES() noexcept;
  void sendFile(const std::string &file, const std::string &fsPath,
                const uint64_t nodeId) const noexcept;
  constexpr bool checkConnection() noexcept;
};
