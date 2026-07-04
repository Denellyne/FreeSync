#include "FSClient.h"
#include <netinet/in.h>

FSClient::FSClient() {
  this->_fd = socket(AF_INET, SOCK_STREAM, 0);

  if (this->_fd < 0) {
    std::println("Creating the socket failed {}", this->_fd);
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(PORT);
  serverAddr.sin_addr.s_addr = INADDR_ANY;

  int connStatus =
      connect(this->_fd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
  if (connStatus < 0) {
    std::println("There was an error making a connection to the server {}",
                 connStatus);
    exit(EXIT_FAILURE);
  }
  this->_pub = std::make_unique<RSAKey>(PUBKEY_PATH);
  if (!this->_pub) {
    std::println("Unable to load public key");
    exit(EXIT_FAILURE);
  }
}

void FSClient::run() {
  while (true) {
    std::string str = "";
    std::getline(std::cin, str);
    SSLString s(str);
    this->writeToSocket(this->_fd, s);
  }
}
