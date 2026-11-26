#pragma once
#include "../FSProtocol.h"
#include <netinet/in.h>
#include <unistd.h>

class FSClientI : public FSProtocol {
public:
  FSClientI() = default;
  FSClientI &operator=(FSClientI &&other) noexcept {
    std::swap(this->_serverAddr, other._serverAddr);
    std::swap(this->_fd, other._fd);
    std::swap(this->_private, other._private);
    std::swap(this->_public, other._public);

    return *this;
  };
  ~FSClientI() noexcept {
    if (this->_fd != -1)
      close(this->_fd);
    this->_fd = -1;
  }

  bool connect() noexcept;
  std::optional<SSLString> issueCommandRead(const SSLString &command) noexcept;
  bool issueCommand(const SSLString &command) noexcept;
  bool downloadFile(const std::string_view outPath) noexcept;

  void disconnect() noexcept {
    if (this->_fd >= 0)
      close(this->_fd);
    this->_fd = -1;
  }

protected:
  int _fd = -1;
  struct sockaddr_in _serverAddr;
  uint8_t _aesLifetime = 0;
  bool validationStep() noexcept;
  bool switchAES() noexcept;
  bool invalidateAES() noexcept;
  void sendFile(const std::string &file, const std::string &fsPath,
                const uint64_t nodeId) const noexcept;
  constexpr bool checkConnection() noexcept;

private:
  bool finishDownload(DiskFile &&file, const std::string_view outPath) noexcept;
};
