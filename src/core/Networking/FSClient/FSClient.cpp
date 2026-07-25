#include "FSClient.h"
#include <netinet/in.h>
#include <print>

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
  volatile bool shouldRun = true;
  {
    std::array<unsigned char, VALIDATIONLENGTH> buf;
    if (!readPacket(this->_fd, buf.data(), VALIDATIONLENGTH)) {
      std::println("Unable to read primitive string");
      return;
    }
    const SSLString str(buf.data(), VALIDATIONLENGTH);
    if (!this->writeToSocket(this->_fd, str)) {
      std::println("Unable to write validation string to socket");
      return;
    }
    if (!readPacket(this->_fd, buf.data(), 4))
      return;
    else {
      std::string command(buf.begin(), buf.begin() + 4);
      std::println("{}", FSPrint(FSStrCode(command)));
      if (FSStrCode(command) == QUIT) {
        shouldRun = false;
        std::println("Closing connection, failed to validate it");
      }
    }
  }

  while (shouldRun) {
    std::string str = "";
    std::getline(std::cin, str);
    SSLString s(str);
    this->writeToSocket(this->_fd, s);
  }
}
