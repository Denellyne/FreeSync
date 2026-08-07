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

  const int connStatus =
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

bool FSClient::validationStep() {
  std::array<unsigned char, VALIDATION_LENGTH> buf;
  if (!readPacket(this->_fd, buf.data(), VALIDATION_LENGTH)) {
    std::println("Unable to read primitive string");
    return false;
  }
  const SSLString str(buf.data(), VALIDATION_LENGTH);
  if (!this->writeToSocket(this->_fd, str)) {
    std::println("Unable to write validation string to socket");
    return false;
  }
  if (!readPacket(this->_fd, buf.data(), COMMAND_LENGTH))
    return false;
  else {
    const std::string command(buf.begin(), buf.begin() + COMMAND_LENGTH);
    if (FSStrCode(command) == QUIT) {
      std::println("Closing connection, failed to validate it");
      return false;
    }
  }
  return true;
}
bool FSClient::switchAES() {
  this->_aes = std::make_unique<AESKey>();
  this->_aesLifetime = 1;
  const SSLString packet = this->generatePacket(AESK, this->_aes->getKey());
  std::println("Packet generated");
  if (!this->writeToSocket(this->_fd, packet)) {
    std::println("Unable to write AESKey to server");
    return false;
  }
  std::println("Switched to AES encryption");
  return true; // Check if received OK

  return false;
}

bool FSClient::invalidateAES() {
  const SSLString packet = this->generatePacket(INV, this->_aes->getKey());
  std::println("Packet generated");
  if (!this->writeToSocketAES(this->_fd, packet)) {
    std::println("Unable to write AESKey to server");
    return false;
  }
  return true;
}

void FSClient::run() {
  volatile bool shouldRun = this->validationStep();
  shouldRun = this->switchAES();

  while (shouldRun) {
    std::string str = "";
    std::getline(std::cin, str);
    const SSLString s(str);
    if (!this->_aes) {
      std::println("Should be in AES, something terribly wrong has happend");
      return;
    }
    this->_aesLifetime++;
    if (!this->_aesLifetime)
      shouldRun = this->invalidateAES() && this->switchAES();

    if (!shouldRun)
      break;

    shouldRun = this->writeToSocketAES(this->_fd, s);
    if (StringOpt msgOpt = readSocketAES(this->_fd); !msgOpt.has_value()) {
      std::println("Unable to get the server answer, closing connection");
      return;
    } else {
      const std::string command = msgOpt->toString();
      std::println("{}", std::string(command.cbegin() + COMMAND_LENGTH + 1,
                                     command.cend()));
    }
  }
}
