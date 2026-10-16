#include "FSClient.h"
#include "../../Compressor/Compressor.h"
#include "../../Node/Node.h"
#include <cmath>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <print>
#include <sys/mman.h>
#include <utility>

FSClient::FSManagerSocket::FSManagerSocket(const std::string &path,
                                           const uint64_t nodeId)
    : _path(std::move(path)), _nodeId(nodeId) {
  this->_fd = socket(AF_INET, SOCK_STREAM, 0);

  if (this->_fd < 0) {
    std::println("Creating the socket failed {}", this->_fd);
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(FSMANAGER_PORT);
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

bool FSClient::FSManagerSocket::validationStep() {
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
bool FSClient::FSManagerSocket::switchAES() {
  this->_aes = std::make_unique<AESKey>();
  const SSLString packet = this->generatePacket(AESK, this->_aes->getKey());
  std::println("Packet generated");
  if (!this->writeToSocket(this->_fd, packet)) {
    std::println("Unable to write AESKey to server");
    return false;
  }
  std::println("Switched to AES encryption");
  return true; // Check if received OK
}

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
std::expected<bool, std::string>
FSClient::validateCommand(const SSLString &command) {
  const FSCode c =
      FSStrCode(std::string_view((char *)command._data, command._length));
  switch (c) {
  case ERR:
  case OK:
  case AESK:
  case INV:
  case AUTH:
  case AES:
    // case PUBK:
    return std::unexpected("The command passed is prohibited to users");
    break;
  case RETR:
    break;
  case STRU: {

    const uint32_t dataViewLength = command._length - COMMAND_LENGTH - 1 > 0
                                        ? command._length - COMMAND_LENGTH - 1
                                        : 0;
    if (dataViewLength == 0)
      return std::unexpected("Need to know what files to send");

    std::span<const unsigned char> dataView = std::span<const unsigned char>(
        command._data + COMMAND_LENGTH + 1, dataViewLength);
    auto split = std::string{dataView.cbegin(), dataView.cend()} |
                 std::views::split(std::string_view{" "}) |
                 std::views::adjacent<2> | std::views::stride(2);
    for (const auto &[file, path] : split) {
      if (!std::filesystem::exists(std::string_view(file)))
        return std::unexpected("File doesn't exist");
    }

  }

  break;
    // Validate files exist
  case QUIT:
  case LIST:
  case DEL:
  case ATTR:
    break;
  }

  return true;
}

bool FSClient::processCommand(const SSLString &command) {

  const FSCode c =
      FSStrCode(std::string_view((char *)command._data, command._length));
  switch (c) {
  default:
    std::unreachable();
  case RETR:
    if (!this->writeToSocketAES(this->_fd, command))
      return false;
    else if (StringOpt msgOpt = readSocketAES(this->_fd); !msgOpt.has_value()) {
      std::println("Unable to get the server answer, closing connection");
      return false;
    } else {
      const std::string msg = msgOpt->toString();
      std::println("{}",
                   std::string(msg.cbegin() + COMMAND_LENGTH + 1, msg.cend()));
    }
    break;
  case STRU: {
    const uint32_t dataViewLength = command._length - COMMAND_LENGTH - 1;

    std::span<const unsigned char> dataView = std::span<const unsigned char>(
        command._data + COMMAND_LENGTH + 1, dataViewLength);
    auto filePaths = std::string{dataView.cbegin(), dataView.cend()} |
                     std::views::split(std::string_view{" "}) |
                     std::views::drop(1) | std::views::stride(2);

    std::string filePathsStr = "";
    for (const auto &fsPath : filePaths)
      filePathsStr =
          filePathsStr + ' ' + std::string{fsPath.begin(), fsPath.end()};

    SSLString request = SSLString(FSCodeStr(FSCode::STRU) + filePathsStr);

    if (!this->writeToSocketAES(this->_fd, request)) {
      std::println("Unable to send request to server");
      return false;
    } else if (StringOpt msgOpt = readSocketAES(this->_fd);
               !msgOpt.has_value()) {
      std::println("Unable to get the server answer, closing connection");
      return false;
    } else {
      const SSLString msg = msgOpt.value();
      if (const FSCode answer =
              FSStrCode(std::string_view((char *)msg._data, COMMAND_LENGTH));
          answer != OK) {
        return false;
      } else {
        if (msg._length != COMMAND_LENGTH + 1 + PTR_SIZE) {
          std::println("Message isn't the node id");
          return false;
        }
        if (const auto idOpt = ptrToNumber(std::span<const unsigned char>(
                msg._data + COMMAND_LENGTH + 1, PTR_SIZE));
            !idOpt.has_value()) {
          std::println("Unable to get node id");
          return false;
        } else {
          const uint64_t nodeId = idOpt.value();
          for (const auto &file :
               std::string{dataView.cbegin(), dataView.cend()} |
                   std::views::split(std::string_view{" "}) |
                   std::views::stride(2)) {

            std::println("{}", std::string(file.begin(), file.end()));
            this->_pool.enqueue(
                std::bind(&FSClient::sendFile, this,
                          std::string(file.begin(), file.end()),
                          nodeId)); // add a way to see if it sent or not
          }
        }
      }
    }
  } break;
  case QUIT:
  case LIST:
  case DEL:
  case ATTR:
    if (!this->writeToSocketAES(this->_fd, command))
      return false;
    else if (StringOpt msgOpt = readSocketAES(this->_fd); !msgOpt.has_value()) {
      std::println("Unable to get the server answer, closing connection");
      return false;
    } else {
      const std::string msg = msgOpt->toString();
      std::println("{}",
                   std::string(msg.cbegin() + COMMAND_LENGTH + 1, msg.cend()));
    }
    break;
  }
  return true;
}
void FSClient::FSManagerSocket::run() {
  if (!validationStep() || !switchAES()) {
    std::println("Unable to validate server");
    return;
  }

  std::println("Sending {}", this->_path);

  const ssize_t fileSize = std::filesystem::file_size(this->_path);
  if (int fd = open(this->_path.c_str(), O_RDONLY, 0); fd < 0) {
    std::cerr << "Unable to open file " << this->_path << "\n";
    return;
  } else {
    unsigned char *buf = (unsigned char *)mmap(NULL, fileSize, PROT_READ,
                                               MAP_FILE | MAP_SHARED, fd, 0);
    madvise(buf, fileSize, MADV_SEQUENTIAL);
    std::span<unsigned char> data(buf, fileSize);
    const std::string hash = Node::hash(data);
    Compressor c(data);

    std::array<unsigned char, PTR_SIZE> ptrArray = ptrToArray(this->_nodeId);
    if (const SSLString packetMsg = SSLString(ptrArray);
        !this->writeToSocketAES(this->_fd, packetMsg))
      return;

    while (!c._finished) {
      std::vector<unsigned char> buf;
      if (!c.compressData(buf)) {
        std::println("Unable to compress chunk");
        return;
      }

      if (const SSLString msg = SSLString(buf);
          !this->writeToSocketAES(this->_fd, msg)) {
        std::println("Failed to send a packet to the server");
        return;
      }
    }
    if (const SSLString msg = SSLString(FSCodeStr(OK));
        !this->writeToSocketAES(this->_fd, msg)) {
      std::println("Failed to send a packet to the server");
      return;
    }
    std::println("Finished sending file");

    if (const SSLString hashMsg = SSLString(hash);
        !this->writeToSocketAES(this->_fd, hashMsg)) {
      std::println("Unable to send hash to server");
      return;
    }

    if (StringOpt msgOpt = readSocketAES(this->_fd); !msgOpt.has_value()) {
      std::println("Unable to get the server answer, closing connection");
      return;
    } else {
      const SSLString msg = msgOpt.value();
      if (const FSCode answer =
              FSStrCode(std::string_view((char *)msg._data, msg._length));
          answer != OK) {
        std::println("Hash was different");
        return;
      }

      std::println("Sent the file successfully");
    }
    munmap(buf, fileSize);
    close(fd);
  }
}

void FSClient::sendFile(const std::string &path, const uint64_t nodeId) {
  FSManagerSocket manager(path, nodeId);
  manager.run();
}

void FSClient::run() {
  volatile bool shouldRun = this->validationStep();
  shouldRun = this->switchAES();

  while (shouldRun) {
    std::string str = "";
    std::getline(std::cin, str);
    SSLString command(str);
    if (auto valOpt = validateCommand(command);
        command._length < 3 || !valOpt.has_value()) {
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

    this->processCommand(command);
  }
}
