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
    std::string command(buf.begin(), buf.begin() + COMMAND_LENGTH);
    std::println("{}", FSPrint(FSStrCode(command)));
    if (FSStrCode(command) == QUIT) {
      std::println("Closing connection, failed to validate it");
      return false;
    }
  }
  return true;
}
bool FSClient::switchAES() {
  this->_aes = std::make_unique<AESKey>();
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

void FSClient::run() {
  volatile bool shouldRun = this->validationStep();
  shouldRun = this->switchAES();

  while (shouldRun) {
    std::string str = "";
    std::getline(std::cin, str);
    const SSLString s(str);
    if (this->_aes)
      shouldRun = this->writeToSocketAES(this->_fd, s);
    else
      shouldRun = this->writeToSocket(this->_fd, s);
  }
}
