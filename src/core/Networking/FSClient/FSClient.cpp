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
  this->_private = std::make_unique<RSAKey>(PKEY_PATH, true);
  if (!this->_private) {
    std::println("Unable to load private key");
    exit(EXIT_FAILURE);
  }
}

bool FSClient::validationStep() {

  std::array<unsigned char, LENGTH_SIZE> serverKeyLength;
  if (!readPacket(this->_fd, serverKeyLength.data(), LENGTH_SIZE)) {
    std::println("Unable to read serverKeyLength");
    return false;
  }
  auto clientKeyLengthOpt = toNumber(serverKeyLength);
  if (!clientKeyLengthOpt.has_value()) {
    std::println("Unable to get key length");
    return false;
  }
  const long clientKeyLength = clientKeyLengthOpt.value();
  std::vector<unsigned char> key(clientKeyLength);
  if (!readPacket(this->_fd, key.data(), clientKeyLength)) {
    std::println("Unable to read key");
    return false;
  }
  this->_public = std::make_unique<RSAKey>(key);
  if (!this->_public) {
    std::println("Unable to load public key");
    return false;
  }
  if (FILE *fp = fopen(PUBKEY_PATH, "r"); !fp) {
    std::println("Unable to open own public key file");
    return false;
  } else {
    fseek(fp, 0, SEEK_END);
    const long length = ftell(fp);
    key.resize(length);
    rewind(fp);
    if (fread(key.data(), sizeof(unsigned char), length, fp) < length) {
      std::println("Unable to read own public key");
      fclose(fp);
      return false;
    }
    fclose(fp);
    std::array<unsigned char, LENGTH_SIZE> lengthArr = toArray(length);
    key.insert(key.begin(), lengthArr.begin(), lengthArr.end());
    if (!writePrimitive(this->_fd, key.data(), LENGTH_SIZE + length)) {
      std::println("Unable to send primitive string");
      return false;
    }
  }
  std::array<unsigned char, COMMAND_LENGTH> buf;
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
std::expected<bool, std::string> FSClient::validateCommand(SSLString &s) {
  const FSCode c = FSStrCode(std::string_view((char *)s._data, s._length));
  switch (c) {

  case RETR:
  case STRU:
    break;
  case ERR:
  case OK:
  case AESK:
  case INV:
  case AUTH:
  case AES:
    // case PUBK:
    return std::unexpected("The command passed is prohibited to users");
    break;
  case QUIT:
  case LIST:
  case DEL:
  case ATTR:
    break;
  }

  return true;
}

void FSClient::run() {
  volatile bool shouldRun = this->validationStep();
  shouldRun = this->switchAES();

  while (shouldRun) {
    std::string str = "";
    std::getline(std::cin, str);
    SSLString s(str);
    if (auto valOpt = validateCommand(s);
        s._length < 3 || !valOpt.has_value()) {
      std::println("{}", valOpt.error());
      continue;
    }

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
