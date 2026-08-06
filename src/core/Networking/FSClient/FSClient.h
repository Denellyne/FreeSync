#pragma once
#include "../FSProtocol.h"
#include <contracts>
#include <print>
#include <unistd.h>
#define PUBKEY_PATH "certs/pub.pem"

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
  int _fd = -1;
  uint8_t _aesLifetime = 0;
  bool validationStep();
  bool switchAES();
  bool invalidateAES() pre(this->_aes);
};
