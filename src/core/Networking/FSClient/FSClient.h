#pragma once
#include "../FSProtocol.h"
#include <print>
#include <unistd.h>
#define PUBKEY_PATH "certs/pub.pem"

class FSClient final : public FSProtocol {
public:
  FSClient();
  ~FSClient() {
    // SEND QUIT
    if (this->_fd != -1)
      close(this->_fd);
    this->_fd = -1;
  }
  virtual void run() override;

private:
  int _fd = -1;
};
