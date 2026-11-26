#include "FSServer.h"
#include "../../Compressor/Compressor.h"
#include "../../Node/LTree.h"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <span>
#include <string>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/select.h>

FSServer::FSServer(std::atomic_bool &running) noexcept
    : _manager(FSManager("localhost", FSMANAGER_PORT, 128)), _running(running) {
  std::error_code ec;
  std::filesystem::create_directory(TEMP_FOLDER, ec);
  if (ec) {
    std::println("Unable to create temporary folder: {}", ec.message());
    this->_error = true;
    return;
  }

  if (const bool res =
          std::filesystem::exists(getCanonicalPath(CLIENTS_CERTS_PATH), ec);
      ec || !res) {
    std::println("Clients folder doesn't exist or another error occured");
    this->_error = true;
    return;
  }
  int opt = 1;
  struct timeval timeout;
  timeout.tv_sec = 10;
  timeout.tv_usec = 0;
  if ((this->_serverFD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
      [[unlikely]] {
    this->_error = true;
    return;
  }

  if (setsockopt(this->_serverFD, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt)) != 0) [[unlikely]] {
    this->_error = true;
    return;
  }
  if (setsockopt(this->_serverFD, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                 sizeof(timeout)) != 0) [[unlikely]] {
    this->_error = true;
    return;
  }
  if (setsockopt(this->_serverFD, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                 sizeof(timeout)) != 0) [[unlikely]] {
    this->_error = true;
    return;
  }

  int yes = 1;
  setsockopt(this->_serverFD, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));

  int idle = 120;
  int intvl = 30;
  int cnt = 5;

  if (setsockopt(this->_serverFD, IPPROTO_TCP, TCP_KEEPIDLE, &idle,
                 sizeof(idle)) != 0) [[unlikely]] {
    this->_error = true;
    return;
  }
  if (setsockopt(this->_serverFD, IPPROTO_TCP, TCP_KEEPINTVL, &intvl,
                 sizeof(intvl)) != 0) [[unlikely]] {
    this->_error = true;
    return;
  }
  if (setsockopt(this->_serverFD, IPPROTO_TCP, TCP_KEEPCNT, &cnt,
                 sizeof(cnt)) != 0) [[unlikely]] {
    this->_error = true;
    return;
  }
  this->_sockAddr.sin_family = AF_INET;
  this->_sockAddr.sin_addr.s_addr = INADDR_ANY;
  this->_sockAddr.sin_port = htons(PORT);
  if (bind(this->_serverFD,
           reinterpret_cast<struct sockaddr *>(&this->_sockAddr),
           sizeof(this->_sockAddr)) < 0) [[unlikely]] {
    this->_error = true;
    return;
  }
  if (!this->_manager) {
    std::println("Manager was unable to start");
    this->_error = true;
    return;
  }
}

void FSServer::run() noexcept {

  this->_pool.enqueue([this] { this->_manager.run(this->_running); });
  std::cout << "Server listening on port " << PORT << '\n';
  if (listen(this->_serverFD, 64) < 0) [[unlikely]] {
    this->_error = true;
    return;
  }
  // socklen_t addrlen = sizeof(this->_sockAddr);

  std::vector<std::shared_ptr<FSServer::Connection>> clientList;

  int clientFd = -1;

  while (this->_running.load()) {
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 1000000;
    fd_set readfds;
    fd_set writefds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    int maxfd = -1;
    if (this->_serverFD >= 0 && this->_serverFD < FD_SETSIZE) {
      FD_SET(this->_serverFD, &readfds);
      maxfd = this->_serverFD;
    }

    for (const auto &client : clientList) {
      const int fd = client->getFd();
      if (fd < 0 || fd >= FD_SETSIZE)
        continue;
      FD_SET(fd, &readfds);
      maxfd = std::max(fd, maxfd);
    }

    if (maxfd < 0)
      continue;

    if (const int activity =
            select(maxfd + 1, &readfds, &writefds, nullptr, &tv);
        activity < 0) {
      std::println("{}", std::strerror(errno));
      continue;
    }
    if (this->_serverFD >= 0 && FD_ISSET(this->_serverFD, &readfds)) {
      clientFd = accept4(this->_serverFD, nullptr, nullptr, 0);
      if (clientFd < 0) {
        std::println("{}", std::strerror(errno));
        continue;
      }

      clientList.emplace_back(std::make_shared<FSServer::Connection>(
          clientFd, std::ref(this->_manager)));
    }

    for (auto it = clientList.begin(); it != clientList.end();) {
      const std::shared_ptr<FSServer::Connection> con = *it;
      const int fd = con->getFd();
      if (con->error() || fd < 0 || fd >= FD_SETSIZE) [[unlikely]] {
        it = clientList.erase(it);
        continue;
      }

      if (!con->isBusy() && FD_ISSET(fd, &readfds)) {
        con->setBusy();
        this->_pool.enqueue([con]() { con->run(); });
      }
      ++it;
    }
  }
}

// Connection

FSServer::Connection::Connection(const int fd, FSManager &manager) noexcept
    : _manager(manager) {
  this->_fd = fd;
  this->_private = std::make_unique<RSAKey>(getCanonicalPath(PKEY_PATH), true);
  if (!this->_private || !this->_private->isOk()) {
    std::println("Unable to load private key");
    this->_error.store(true);
    return;
  }

  struct timeval timeout;
  timeout.tv_sec = 15;
  timeout.tv_usec = 0;
  if (setsockopt(this->_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                 sizeof(timeout)) != 0) [[unlikely]] {
    this->_error.store(true);
    return;
  }
  if (setsockopt(this->_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                 sizeof(timeout)) != 0) [[unlikely]] {
    this->_error.store(true);
    return;
  }

  int yes = 1;
  setsockopt(this->_fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));

  int idle = 3;
  int intvl = 5;
  int cnt = 5;

  if (setsockopt(this->_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) !=
      0) [[unlikely]] {
    this->_error.store(true);
    return;
  }
  if (setsockopt(this->_fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl,
                 sizeof(intvl)) != 0) [[unlikely]] {
    this->_error.store(true);
    return;
  }
  if (setsockopt(this->_fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt)) != 0)
      [[unlikely]] {
    this->_error.store(true);
    return;
  }

  std::println("Initializing validation step");
  if (!handleValidation()) {
    this->_error.store(true);
    const std::string command = FSCodeStr(QUIT);
    if (!FSServer::Connection::writePrimitive(
            this->_fd, (void *)command.c_str(), command.length()))
      return;

    std::println("Validation step failed");
    return;
  }
  const std::string command = FSCodeStr(OK);
  if (!FSServer::Connection::writePrimitive(this->_fd, (void *)command.c_str(),
                                            command.length())) {
    this->_error.store(true);
    return;
  }
  std::println("Validation step success");
}

bool FSServer::Connection::interpretCommandRSA(
    const SSLString &command) noexcept {
  if (command._length < COMMAND_LENGTH)
    return false;
  const std::string_view codeView(
      reinterpret_cast<const char *>(command._data),
      reinterpret_cast<const char *>(command._data + COMMAND_LENGTH));

  const FSCode code = FSStrCode(codeView);
  switch (code) {
  case AESK: {
    if (command._length < AES_KEY_BYTES + COMMAND_LENGTH)
      return false;
    this->_aes = std::make_unique<AESKey>(command._data + COMMAND_LENGTH);
    if (!changeTimeout())
      return false;

    std::println("Switched to AES encryption");
  } break;
  case OK:
    break;
  case QUIT:
    return false;
    break;
  default:
    std::println("Invalid commmand for RSA communication");
    return false;
    break;
  case ERR:
    std::println("Error received");
    return false;
    break;
  }
  return true;
}
bool FSServer::Connection::interpretCommand(const SSLString &command) noexcept {
  if (command._length < COMMAND_LENGTH)
    return false;
  const std::string_view codeView(
      reinterpret_cast<const char *>(command._data),
      reinterpret_cast<const char *>(command._data + COMMAND_LENGTH));
  const uint32_t dataViewLength = command._length > COMMAND_LENGTH + 1
                                      ? command._length - COMMAND_LENGTH - 1
                                      : 0;
  std::span<const unsigned char> dataView{};
  if (dataViewLength > 0)
    dataView = std::span<const unsigned char>(
        command._data + COMMAND_LENGTH + 1, dataViewLength);

  const FSCode code = FSStrCode(codeView);
  switch (code) {
  case DEL: {
    if (dataView.empty())
      return sendErrorMessage("No path was passed");

    auto split =
        std::string{dataView.cbegin(), dataView.cend()} |
        std::views::split(' ') |
        std::views::transform([](auto &&rg) { return std::string_view{rg}; }) |
        std::ranges::to<std::vector<std::string>>();
    std::vector<std::string> context = {};
    std::vector<Request> requests;
    for (const auto &file : split) {
      if (const auto valOpt = Node::validatePath(file, context);
          !valOpt.has_value())
        return sendErrorMessage("Path must be a valid path: " +
                                std::string(valOpt.error()));
      requests.emplace_back(file);
    }

    this->_manager.addNewNode(std::move(requests));
    return sendReply("Delete transaction sent successfully");
  } break;
  case RETR: {
    if (dataView.empty())
      return sendErrorMessage("No path was passed");

    if (const auto current = Node::getHeadFile(); !current.has_value()) {
      std::println("Unable to get current head file");
      break;
    } else {
      alignas(ALIGNMENT) std::array<char, 64> arr;
      memcpy(arr.data(), current.value().data(), 64);
      LTree tree = LTree(arr, "/FreeSync", true);
      if (auto fileOpt = tree.getChildFile(
              std::string{dataView.cbegin(), dataView.cend()});
          !fileOpt.has_value())
        return sendErrorMessage(fileOpt.error());
      else {
        std::println("Sending {}",
                     std::string{dataView.cbegin(), dataView.cend()});
        DiskFile file = std::move(fileOpt.value());
        const std::string hash = Node::hash(file.toSpan());
        Compressor comp(file.toSpan());

        while (!comp._finished) {
          const std::string dataHeader = FSCodeStr(DATA);
          std::vector<unsigned char> buf;
          if (!comp.compressData(buf))
            return sendErrorMessage("Unable to compress chunk");
          buf.insert(buf.begin(), dataHeader.begin(), dataHeader.end());

          if (const SSLString msg = SSLString(buf);
              !this->writeToSocketAES(this->_fd, msg)) {
            std::println("Failed to send a packet to the client");
            return false;
          }
        }

        if (const SSLString msg = SSLString(FSCodeStr(OK));
            !this->writeToSocketAES(this->_fd, msg)) {
          std::println("Failed to send a packet to the client");
          return false;
        }
        std::println("Finished sending file");

        if (const SSLString hashMsg = SSLString(hash);
            !this->writeToSocketAES(this->_fd, hashMsg)) {
          std::println("Unable to send hash to client");
          return false;
        }
      }
    }
  } break;
  case STRU: {
    if (dataView.empty())
      return sendErrorMessage("No path was passed");

    auto split =
        std::string{dataView.cbegin(), dataView.cend()} |
        std::views::split(' ') |
        std::views::transform([](auto &&rg) { return std::string_view{rg}; }) |
        std::ranges::to<std::vector<std::string>>();

    if (std::ranges::distance(split) % 3 != 0)
      return sendErrorMessage("Every file should have a corresponding server "
                              "path and also a executable information");

    auto pairView = split | std::views::adjacent<3> | std::views::stride(3);

    std::vector<Request> requests;

    std::vector<std::string> context = {};
    for (const auto &[file, fsPath, executable] : pairView) {

      if (const auto valOpt = Node::validatePath(fsPath, context);
          !valOpt.has_value())
        return sendErrorMessage("Path must be a valid path: " +
                                std::string(valOpt.error()));
      else
        requests.emplace_back(std::move(file), std::move(fsPath),
                              executable[0] == '1');
    }

    const uint64_t id = this->_manager.addNewNode(std::move(requests));
    alignas(32) const std::array<unsigned char, PTR_SIZE> numberArray =
        ptrToArray(id);
    return sendReply(std::string{numberArray.begin(), numberArray.end()});

  } break;
  case ATTR: {
    if (dataView.empty())
      return sendErrorMessage("No path was passed");

    if (const auto current = Node::getHeadFile(); !current.has_value()) {
      std::println("Unable to get current head file");
      break;
    } else {
      alignas(ALIGNMENT) std::array<char, 64> arr;
      memcpy(arr.data(), current.value().data(), 64);
      LTree tree = LTree(arr, "/FreeSync", true);
      const std::string path(dataView.cbegin(), dataView.cend());
      const size_t filePos = path.find_last_of('/');
      if (filePos == std::string::npos)
        return sendErrorMessage("Path must be a valid path");
      const std::string dirPath = path.substr(0, filePos);
      const std::string entry = path.substr(filePos + 1, path.size());
      if (const auto treeOpt = tree.getChildTree(dirPath); !treeOpt.has_value())
        return sendErrorMessage("Unable to find folder");
      else {
        for (const auto &child : treeOpt->getChildren())
          if (child._fileName == entry) {
            std::string msgRaw;
            std::string type = "FILE ";
            std::string nlink = "1 ";
            const std::string fileSize =
                std::to_string(child.getFileSize()) + ' ';
            const std::string modificationDate =
                std::to_string(child.getModificationDate());
            if (child._entry == DIRECTORY) {
              type = "DIRECTORY ";

              alignas(ALIGNMENT) std::array<char, 64> hash;
              memcpy(hash.data(), child._hash.data(), 64);
              const LTree subTree = LTree(
                  hash, treeOpt->getFileName() + '/' + child._fileName, false);
              if (const auto dirOpt = subTree.getChildTree(entry);
                  !dirOpt.has_value())
                return sendErrorMessage("Unable to find folder");
              else
                nlink = std::to_string(2 + dirOpt->getChildren().size()) + " ";
            } else {
              if (child._entry != REGULAR_FILE)
                type = "EXECUTABLE ";
            }
            msgRaw += type;
            msgRaw += nlink;
            msgRaw += fileSize;
            msgRaw += modificationDate;
            return sendReply(msgRaw);
          }
        return sendErrorMessage("Unable to find folder");
      }
    }
  } break;
  case INV: {
    this->_aes = nullptr;
  } break;
  case AESK: {
    if (command._length < AES_KEY_BYTES + COMMAND_LENGTH)
      return false;
    this->_aes = std::make_unique<AESKey>(command._data + COMMAND_LENGTH);
    std::println("Switched to AES encryption");
  } break;
  case ITER: {

    if (dataView.empty())
      return sendErrorMessage("No path was passed");

    if (const auto current = Node::getHeadFile(); !current.has_value()) {
      std::println("Unable to get current head file");
      break;
    } else {
      alignas(ALIGNMENT) std::array<char, 64> arr;
      memcpy(arr.data(), current.value().data(), 64);
      LTree tree = LTree(arr, "/FreeSync", true);
      const std::string path(dataView.cbegin(), dataView.cend());
      if (const auto treeOpt = tree.getChildTree(path); !treeOpt.has_value())
        return sendErrorMessage("Unable to find folder");
      else {
        std::string msgRaw;
        const auto &children = treeOpt.value().getChildren();
        for (const auto &child : children)
          msgRaw += child._fileName + '\n';

        return sendReply(msgRaw);
      }
    }
  } break;
  case LIST: {
    if (dataView.empty())
      return sendErrorMessage("No path was passed");

    if (const auto current = Node::getHeadFile(); !current.has_value()) {
      std::println("Unable to get current head file");
      break;
    } else {
      alignas(ALIGNMENT) std::array<char, 64> arr;
      memcpy(arr.data(), current.value().data(), 64);
      LTree tree = LTree(arr, "/FreeSync", true);
      const std::string path(dataView.cbegin(), dataView.cend());
      if (const auto treeOpt = tree.getChildTree(path); !treeOpt.has_value())
        return sendErrorMessage("Unable to find folder");
      else {
        std::string msgRaw;
        const auto &children = treeOpt.value().getChildren();
        for (const auto &child : children) {
          std::string entry = "File - ";
          std::string fileSize = "0 - ";
          if (child._entry == DIRECTORY)
            entry = "Directory - ";
          else {
            fileSize = prettyFileSize(child.getFileSize()) + " - ";
            if (child._entry != REGULAR_FILE)
              entry = "Executable - ";
          }
          msgRaw += entry + fileSize + child._fileName + '\n';
        }
        return sendReply(msgRaw);
      }
    }

  } break;
  case MOV:
    break;
  case OK:
    break;

  case QUIT:
    return false;
    break;

  case ERR:
    std::println("{}", command.toString());
    std::println("Error received");
    return false;
    break;
  case DATA:
    std::println("Shouldn't be receiving a data packet");
    return false;
  case PING:
  case AUTH:
  case AES:
    break;
  case HASH:
    if (dataView.empty())
      return sendErrorMessage("No path was passed");

    if (const auto current = Node::getHeadFile(); !current.has_value()) {
      std::println("Unable to get current head file");
      break;
    } else {
      alignas(ALIGNMENT) std::array<char, 64> arr;
      memcpy(arr.data(), current.value().data(), 64);
      LTree tree = LTree(arr, "/FreeSync", true);
      const std::string path(dataView.cbegin(), dataView.cend());
      if (const auto &fileOpt = tree.getChildFile(path); !fileOpt.has_value())
        return sendErrorMessage("Unable to get final decompressed file");
      else if (const DiskFile &file = fileOpt.value(); !file)
        return sendErrorMessage("File found an error");
      else
        return sendReply(Node::hash(file.toSpan()));
    }
    return sendErrorMessage("Unable to find child");
    break;
  }
  return true;
}
void FSServer::Connection::run() noexcept {

  if (!this->_aes) {
    if (auto commandOpt = readSocketRSA(this->_fd); !commandOpt.has_value()) {
      this->_error.store(true);
      this->_busy.store(false);
      return;
    } else {
      if (!interpretCommandRSA(commandOpt.value())) {
        this->_error.store(true);
        this->_busy.store(false);
        return;
      }
    }
    this->_busy.store(false);
    // std::cout << command << '\n';
    return;
  }
  if (StringOpt commandOpt = readSocketAES(this->_fd);
      !commandOpt.has_value()) {
    this->_error.store(true);
    this->_busy.store(false);
    return;
  } else {
    if (!interpretCommand(commandOpt.value())) {
      this->_error.store(true);
      this->_busy.store(false);
      return;
    }
    this->_busy.store(false);
  }
  this->_busy.store(false);
}
