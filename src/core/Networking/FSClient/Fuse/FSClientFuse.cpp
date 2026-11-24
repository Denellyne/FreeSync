#include "FSClientFuse.h"
#include <arpa/inet.h>

FSClientFuse::FSClientFuse(const std::string_view ip) noexcept {

  std::error_code ec;
  std::filesystem::create_directory(TEMP_FOLDER, ec);
  if (ec) {
    std::println("Unable to create temporary folder: {}", ec.message());
    exit(EXIT_FAILURE);
  }

  this->_serverAddr.sin_family = AF_INET;
  this->_serverAddr.sin_port = htons(PORT);
  this->_serverAddr.sin_addr.s_addr = inet_addr(ip.data());

  this->_private = std::make_unique<RSAKey>(getCanonicalPath(PKEY_PATH), true);
  if (!this->_private || !this->_private->isOk()) {
    std::println("Unable to load private key");
    exit(EXIT_FAILURE);
  }
}
