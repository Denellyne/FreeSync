#pragma once
#include "../FSClientI.h"
#include <netinet/in.h>
#include <unistd.h>

class FSClientFuse final : public FSClientI {
public:
  FSClientFuse(const std::string_view ip) noexcept;
  FSClientFuse() = delete;
  FSClientFuse &operator=(FSClientFuse &&other) {
    std::swap(this->_serverAddr, other._serverAddr);
    std::swap(this->_fd, other._fd);
    std::swap(this->_private, other._private);
    std::swap(this->_public, other._public);

    return *this;
  };
  ~FSClientFuse() noexcept {
    if (this->_fd != -1)
      close(this->_fd);
    this->_fd = -1;
  }
  virtual void run() noexcept override {};
  std::optional<SSLString> readPacket() noexcept {
    return this->readSocketAES(this->_fd);
  }

private:
};
