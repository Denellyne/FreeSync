#include "FSClient.h"
#include "../../../Compressor/Compressor.h"
#include "../../../Node/Node.h"
#include <arpa/inet.h>
#include <cassert>
#include <fcntl.h>
#include <filesystem>
#include <netinet/in.h>
#include <print>
#include <sys/mman.h>
#include <utility>

using std::filesystem::file_size;

FSClient::FSClient(const std::string_view ip) noexcept {

  std::error_code ec;
  std::filesystem::create_directory(TEMP_FOLDER, ec);
  if (ec) {
    std::println("Unable to create temporary folder: {}", ec.message());
    exit(EXIT_FAILURE);
  }

  this->_serverAddr.sin_family = AF_INET;
  this->_serverAddr.sin_port = htons(PORT);
  this->_serverAddr.sin_addr.s_addr = inet_addr(ip.data());
  FSClient::getIp(ip);

  this->_private = std::make_unique<RSAKey>(getCanonicalPath(PKEY_PATH), true);
  if (!this->_private || !this->_private->isOk()) {
    std::println("Unable to load private key");
    exit(EXIT_FAILURE);
  }
}
FSClient::FSManagerSocket::FSManagerSocket(std::string &&file,
                                           std::string &&fsPath,
                                           const uint64_t nodeId)
    : _file(file), _fsPath(fsPath), _nodeId(nodeId) {

  this->_serverAddr.sin_family = AF_INET;
  this->_serverAddr.sin_port = htons(FSMANAGER_PORT);
  this->_serverAddr.sin_addr.s_addr = inet_addr(FSClient::getIp().data());

  this->_private = std::make_unique<RSAKey>(getCanonicalPath(PKEY_PATH), true);
  if (!this->_private || !this->_private->isOk()) {
    std::println("Unable to load private key");
    exit(EXIT_FAILURE);
  }
}

std::expected<bool, std::string_view>
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
  default:
    return false;
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
  case ITER:
  case LIST:
  case ATTR:
  case PING:
    break;
  case DATA:
    return std::unexpected("Shouldn't be receiving a data packet");
    break;
  }

  return true;
}
bool FSClient::handleSendFile(
    const std::span<const unsigned char> dataView) noexcept {

  auto split =
      std::string{dataView.cbegin(), dataView.cend()} | std::views::split(' ') |
      std::views::transform([](auto &&rg) { return std::string_view{rg}; }) |
      std::ranges::to<std::vector<std::string>>() | std::views::adjacent<2> |
      std::views::stride(2);
  std::string raw;
  for (const auto &[file, fsPath] : split) {
    char isExecutable = '0';
    if (const fs::perms permissions = fs::status(file).permissions();
        (fs::perms::none != (permissions & fs::perms::owner_exec)) ||
        (fs::perms::none != (permissions & fs::perms::group_exec)) ||
        (fs::perms::none != (permissions & fs::perms::others_exec)))
      isExecutable = '1';

    raw += ' ';
    raw += file;
    raw += ' ';
    raw += fsPath;
    raw += ' ';
    raw += isExecutable;
  }

  if (StringOpt msgOpt = issueCommandRead(SSLString(FSCodeStr(STRU) + raw));
      !msgOpt.has_value()) {
    std::println("Unable to get the server answer, closing connection");
    return false;
  } else {
    const SSLString msg = std::move(msgOpt.value());
    if (const FSCode answer =
            FSStrCode(std::string_view((char *)msg._data, COMMAND_LENGTH));
        answer != OK) {
      const std::string msg = msgOpt->toString();
      std::println("{}",
                   std::string(msg.cbegin() + COMMAND_LENGTH + 1, msg.cend()));
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
          this->_pool.enqueue([nodeId, file, fsPath] {
            sendFile(file, fsPath, nodeId);
          }); // add a way to see if it sent or not
        }
      }
    }
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
    std::println("Illegal instruction");
    return false;
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
    }
    return downloadFile(split[1]);

  } break;
  case STRU:
    return this->handleSendFile(dataView);
    break;
  case QUIT:
  case ITER:
  case LIST:
  case DEL:
  case ATTR:
    if (StringOpt msgOpt = issueCommandRead(command); !msgOpt.has_value()) {
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
  if (!connect()) {
    std::println("Unable to connect to the server");
    return;
  }

  std::println("Sending {}", this->_file);

  const size_t fileSize = file_size(this->_file);
  if (int fd = open(this->_file.c_str(), O_RDONLY, 0); fd < 0) {
    std::cerr << "Unable to open file " << this->_file << "\n";
    return;
  } else {
    unsigned char *buf = static_cast<unsigned char *>(
        mmap(nullptr, fileSize, PROT_READ, MAP_FILE | MAP_SHARED, fd, 0));
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

void FSClient::sendFile(std::string file, std::string fsPath,
                        const uint64_t nodeId) noexcept {
  FSManagerSocket manager(std::move(file), std::move(fsPath), nodeId);
  manager.run();
}

void FSClient::run() noexcept {
  if (!connect()) {
    std::println("Unable to connect to server");
    return;
  }

  volatile bool shouldRun = true;
  while (shouldRun) {
    std::string str;
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
    if (this->_aesLifetime == 0)
      shouldRun = this->invalidateAES() && this->switchAES();

    if (!shouldRun)
      break;

    this->processCommand(command);
  }
}
