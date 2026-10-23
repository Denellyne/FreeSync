#include "FSClient.h"
#include "../../Compressor/Compressor.h"
#include "../../Node/Node.h"
#include <cassert>
#include <fcntl.h>
#include <filesystem>
#include <netinet/in.h>
#include <print>
#include <sys/mman.h>
#include <utility>

FSClient::FSManagerSocket::FSManagerSocket(const std::string &file,
                                           const std::string &fsPath,
                                           const uint64_t nodeId)
    : _file(std::move(file)), _fsPath(std::move(fsPath)), _nodeId(nodeId) {
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
  if (!this->_private || !this->_private->isOk()) {
    std::println("Unable to load private key");
    exit(EXIT_FAILURE);
  }
}

bool FSClient::FSManagerSocket::validationStep() noexcept {
  alignas(32) std::array<unsigned char, LENGTH_SIZE> serverKeyLength;
  if (!readPacket(this->_fd, serverKeyLength.data(), LENGTH_SIZE)) {
    std::println("Unable to read serverKeyLength");
    return false;
  }
  const auto clientKeyLengthOpt = toNumber(serverKeyLength);
  if (!clientKeyLengthOpt.has_value()) {
    std::println("Unable to get key length");
    return false;
  }
  const size_t clientKeyLength = clientKeyLengthOpt.value();
  std::vector<unsigned char> key(clientKeyLength);
  if (!readPacket(this->_fd, key.data(), clientKeyLength)) {
    std::println("Unable to read key");
    return false;
  }
  this->_public = std::make_unique<RSAKey>(key);
  if (!this->_public || !this->_public->isOk()) {
    std::println("Unable to load public key");
    return false;
  }
  if (FILE *fp = fopen(PUBKEY_PATH, "r"); !fp) {
    std::println("Unable to open own public key file");
    return false;
  } else {
    fseek(fp, 0, SEEK_END);
    const size_t length = ftell(fp);
    key.resize(length);
    rewind(fp);
    if (fread(key.data(), sizeof(unsigned char), length, fp) < (size_t)length) {
      std::println("Unable to read own public key");
      fclose(fp);
      return false;
    }
    fclose(fp);
    alignas(32) std::array<unsigned char, LENGTH_SIZE> lengthArr =
        toArray(length);
    key.insert(key.begin(), lengthArr.begin(), lengthArr.end());
    if (!writePrimitive(this->_fd, key.data(), LENGTH_SIZE + length)) {
      std::println("Unable to send primitive string");
      return false;
    }
  }
  std::array<unsigned char, COMMAND_LENGTH> buf;
  if (!readPacket(this->_fd, buf.data(), COMMAND_LENGTH)) {
    std::println("Unable to read server answer");
    return false;
  } else {
    const std::string command(buf.begin(), buf.begin() + COMMAND_LENGTH);
    if (FSStrCode(command) == QUIT) {
      std::println("Closing connection, failed to validate it");
      return false;
    }
  }
  return true;
}
bool FSClient::FSManagerSocket::switchAES() noexcept {
  this->_aes = std::make_unique<AESKey>();
  if (!this->_aes || !this->_aes->isOk()) {
    std::println("Unable to generate AES key");
    return false;
  }

  const SSLString packet = this->generatePacket(AESK, this->_aes->getKey());
  std::println("Packet generated");
  if (!this->writeToSocket(this->_fd, packet)) {
    std::println("Unable to write AESKey to server");
    return false;
  }
  std::println("Switched to AES encryption");
  return true; // Check if received OK
}

FSClient::FSClient() noexcept {
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
  if (!this->_private || !this->_private->isOk()) {
    std::println("Unable to load private key");
    exit(EXIT_FAILURE);
  }
}

bool FSClient::validationStep() noexcept {

  alignas(32) std::array<unsigned char, LENGTH_SIZE> serverKeyLength;
  if (!readPacket(this->_fd, serverKeyLength.data(), LENGTH_SIZE)) {
    std::println("Unable to read serverKeyLength");
    return false;
  }
  auto clientKeyLengthOpt = toNumber(serverKeyLength);
  if (!clientKeyLengthOpt.has_value()) {
    std::println("Unable to get key length");
    return false;
  }
  const size_t clientKeyLength = clientKeyLengthOpt.value();
  if (clientKeyLength > 1024) {
    std::println("Key is to large to be a RSA Key");
    return false;
  }

  std::vector<unsigned char> key(clientKeyLength);
  if (!readPacket(this->_fd, key.data(), clientKeyLength)) {
    std::println("Unable to read key");
    return false;
  }
  this->_public = std::make_unique<RSAKey>(key);
  if (!this->_public || !this->_public->isOk()) {
    std::println("Unable to load public key");
    return false;
  }
  if (FILE *fp = fopen(PUBKEY_PATH, "r"); !fp) {
    std::println("Unable to open own public key file");
    return false;
  } else {
    fseek(fp, 0, SEEK_END);
    const size_t length = ftell(fp);
    key.resize(length);
    rewind(fp);
    if (fread(key.data(), sizeof(unsigned char), length, fp) < (size_t)length) {
      std::println("Unable to read own public key");
      fclose(fp);
      return false;
    }
    fclose(fp);
    alignas(32) std::array<unsigned char, LENGTH_SIZE> lengthArr =
        toArray(length);
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
bool FSClient::switchAES() noexcept {
  this->_aes = std::make_unique<AESKey>();
  if (!this->_aes || !this->_aes->isOk()) {
    std::println("Unable to generate AES key");
    return false;
  }
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

bool FSClient::invalidateAES() noexcept {
  assert(this->_aes);
  const SSLString packet = this->generatePacket(INV, this->_aes->getKey());
  std::println("Packet generated");
  if (!this->writeToSocketAES(this->_fd, packet)) {
    std::println("Unable to write AESKey to server");
    return false;
  }
  return true;
}
std::expected<bool, std::string>
FSClient::validateCommand(const SSLString &command) noexcept {
  if (command._length < 3 || (command._length > 3 && command[3] != ' '))
    return std::unexpected("String is too small to be a valid command");

  const uint32_t dataViewLength = command._length - COMMAND_LENGTH - 1 > 0
                                      ? command._length - COMMAND_LENGTH - 1
                                      : 0;

  std::span<const unsigned char> dataView = std::span<const unsigned char>(
      command._data + COMMAND_LENGTH + 1, dataViewLength);
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
  case DEL: {
    if (dataViewLength == 0)
      return std::unexpected("Need to know what files to delete");
  } break;

  case RETR: {

    if (dataViewLength == 0)
      return std::unexpected("Need to know what file to read");

    std::vector<std::string> split =
        std::string{dataView.cbegin(), dataView.cend()} |
        std::views::split(' ') |
        std::views::transform([](auto &&rg) { return std::string_view{rg}; }) |
        std::ranges::to<std::vector<std::string>>();
    if (split.size() != 2)
      return std::unexpected(
          "Need to know what file to read and what file to write to");

    // Needs path for file
  } break;
  case STRU: {

    if (dataViewLength == 0)
      return std::unexpected("Need to know what files to send");

    auto split =
        std::string{dataView.cbegin(), dataView.cend()} |
        std::views::split(' ') |
        std::views::transform([](auto &&rg) { return std::string_view{rg}; }) |
        std::ranges::to<std::vector<std::string>>();

    if (std::ranges::distance(split) % 2 != 0)
      return std::unexpected(
          "Every file should have a corresponding server path");

    auto pairView = split | std::views::adjacent<2> | std::views::stride(2);
    for (const auto &[file, path] : pairView) {
      if (!std::filesystem::exists(file))
        return std::unexpected("File doesn't exist:" + file);
    }

  }

  break;
    // Validate files exist
  case QUIT:
  case LIST:
  case ATTR:
    break;
  case DATA:
    return std::unexpected("Shouldn't be receiving a data packet");
    break;
  }

  return true;
}

bool FSClient::processCommand(const SSLString &command) noexcept {

  const uint32_t dataViewLength = command._length - COMMAND_LENGTH - 1;

  std::span<const unsigned char> dataView = std::span<const unsigned char>(
      command._data + COMMAND_LENGTH + 1, dataViewLength);
  const FSCode c =
      FSStrCode(std::string_view((char *)command._data, command._length));
  switch (c) {
  default:
    std::unreachable();
  case RETR: {

    std::vector<std::string> split =
        std::string{dataView.cbegin(), dataView.cend()} |
        std::views::split(' ') |
        std::views::transform([](auto &&rg) { return std::string_view{rg}; }) |
        std::ranges::to<std::vector<std::string>>();

    if (!this->writeToSocketAES(this->_fd,
                                SSLString(FSCodeStr(RETR) + ' ' + split[0]))) {
      std::println("Unable to send request to server");
      return false;
    } else {
      DiskFile file;
      if (!file)
        std::println("Unable to create file: {} {}", std::strerror(errno),
                     file.getFilePath());

      while (true) {
        if (StringOpt sslOpt = readSocketAES(this->_fd); !sslOpt.has_value()) {
          std::println("Unable to read from server");
          return false;
        } else {
          SSLString ssl = std::move(sslOpt.value());
          const std::span<const unsigned char> view{ssl._data, ssl._length};
          if (view.size() < 3) {
            std::println("Buffer is too small, an error occured");
            continue;
          }

          if (FSStrCode(std::string_view((char *)view.data(), 3)) ==
              FSCode::OK) {
            if (StringOpt msgOpt = readSocketAES(this->_fd);
                !msgOpt.has_value()) {
              std::println("Unable to get the server hash, closing connection");
              return false;
            } else {
              if (!file)
                return false;
              const SSLString msg = std::move(msgOpt.value());
              file = decompressData(file.toSpan());
              if (!file) {
                std::println("Unable to decompress file");
                return false;
              }
              const std::string hash = Node::hash(file.toSpan());
              if (msg._length != hash.size()) {
                std::println("Message has different length than our hash");
                return false;
              }
              std::span<const unsigned char> dataView =
                  std::span<const unsigned char>(msg._data, msg._length);
              if (std::string(dataView.begin(), dataView.end()) != hash) {
                std::println("Hash is not the same");
                return false;
              }

              DiskFile out(split[1], true);
              if (!out)
                std::println("Unable to create file: {} {}",
                             std::strerror(errno), out.getFilePath());
              return out.write(file.toSpan());
            }
            std::println("Finished receiving file");
            return true;
          } else if (FSStrCode(std::string_view((char *)view.data(), 3)) ==
                     FSCode::ERR) {
            const std::string err = std::string(view.begin() + 3, view.end());
            std::println("Error from server:{}", err);
            return false;
          } else if (FSStrCode(std::string_view((char *)view.data(), 3)) ==
                         FSCode::DATA &&
                     file && !file.write(view.subspan(3))) {
            std::println("Unable to write to temporary file");
            return false;
          }
        }
      }
    }
  } break;
  case STRU: {
    auto split =
        std::string{dataView.cbegin(), dataView.cend()} |
        std::views::split(' ') |
        std::views::transform([](auto &&rg) { return std::string_view{rg}; }) |
        std::ranges::to<std::vector<std::string>>() | std::views::adjacent<2> |
        std::views::stride(2);
    std::string raw = "";
    for (const auto &[file, fsPath] : split) {
      char isExecutable = '0';
      if (const fs::perms permissions = fs::status(file).permissions();
          (fs::perms::none != (permissions & fs::perms::owner_exec)) ||
          (fs::perms::none != (permissions & fs::perms::group_exec)) ||
          (fs::perms::none != (permissions & fs::perms::others_exec)))
        isExecutable = '1';

      raw += ' ' + file + ' ' + fsPath + ' ' + isExecutable;
    }

    if (!this->writeToSocketAES(this->_fd, SSLString(FSCodeStr(STRU) + raw))) {
      std::println("Unable to send request to server");
      return false;
    } else if (StringOpt msgOpt = readSocketAES(this->_fd);
               !msgOpt.has_value()) {
      std::println("Unable to get the server answer, closing connection");
      return false;
    } else {
      const SSLString msg = std::move(msgOpt.value());
      if (const FSCode answer =
              FSStrCode(std::string_view((char *)msg._data, COMMAND_LENGTH));
          answer != OK) {
        const std::string msg = msgOpt->toString();
        std::println(
            "{}", std::string(msg.cbegin() + COMMAND_LENGTH + 1, msg.cend()));
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
          for (const auto &[file, fsPath] : split) {
            this->_pool.enqueue(
                std::bind(&FSClient::sendFile, this, file, fsPath,
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
void FSClient::FSManagerSocket::run() noexcept {
  if (!validationStep() || !switchAES()) {
    std::println("Unable to validate server");
    return;
  }

  std::println("Sending {}", this->_file);

  const ssize_t fileSize = std::filesystem::file_size(this->_file);
  if (int fd = open(this->_file.c_str(), O_RDONLY, 0); fd < 0) {
    std::cerr << "Unable to open file " << this->_file << "\n";
    return;
  } else {
    unsigned char *buf = (unsigned char *)mmap(NULL, fileSize, PROT_READ,
                                               MAP_FILE | MAP_SHARED, fd, 0);
    madvise(buf, fileSize, MADV_SEQUENTIAL);
    std::span<unsigned char> data(buf, fileSize);
    const std::string hash = Node::hash(data);
    Compressor c(data);
    if (const SSLString msg = SSLString(this->_file);
        !this->writeToSocketAES(this->_fd, msg)) {
      std::println("Failed to send a packet to the server");
      return;
    }

    std::array<unsigned char, PTR_SIZE> ptrArray = ptrToArray(this->_nodeId);
    if (const SSLString packetMsg = SSLString(ptrArray);
        !this->writeToSocketAES(this->_fd, packetMsg))
      return;

    while (!c._finished) {
      const std::string dataHeader = FSCodeStr(DATA);
      std::vector<unsigned char> buf;
      if (!c.compressData(buf)) {
        std::println("Unable to compress chunk");
        return;
      }
      buf.insert(buf.begin(), dataHeader.begin(), dataHeader.end());

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
      const SSLString msg = std::move(msgOpt.value());
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

void FSClient::sendFile(const std::string &file, const std::string &fsPath,
                        const uint64_t nodeId) const noexcept {
  FSManagerSocket manager(file, fsPath, nodeId);
  manager.run();
}

void FSClient::run() noexcept {
  volatile bool shouldRun = this->validationStep();
  shouldRun &= this->switchAES();

  while (shouldRun) {
    std::string str = "";
    if (!std::getline(std::cin, str))
      break;

    SSLString command(str);
    if (auto valOpt = validateCommand(command);
        command._length < 3 || !valOpt.has_value()) {
      std::println("{}", valOpt.error_or("Command was too short"));
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
