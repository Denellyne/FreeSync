#include "FSServer.h"
#include "../../Compressor/Compressor.h"
#include "../../Node/LTree.h"
#include <cstring>
#include <fcntl.h>
#include <span>
#include <string>
#include <sys/mman.h>
#include <sys/random.h>
#include <thread>

FSServer::FSServer(std::atomic_bool &running) noexcept
    : _manager(FSManager("localhost", FSMANAGER_PORT, 128)), _running(running) {
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
                 sizeof(opt))) [[unlikely]] {
    this->_error = true;
    return;
  }
  if (setsockopt(this->_serverFD, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                 sizeof(timeout))) [[unlikely]] {
    this->_error = true;
    return;
  }
  if (setsockopt(this->_serverFD, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                 sizeof(timeout))) [[unlikely]] {
    this->_error = true;
    return;
  }

  int yes = 1;
  setsockopt(this->_serverFD, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));

  int idle = 120;
  int intvl = 30;
  int cnt = 5;

  if (setsockopt(this->_serverFD, IPPROTO_TCP, TCP_KEEPIDLE, &idle,
                 sizeof(idle))) [[unlikely]] {
    this->_error = true;
    return;
  }
  if (setsockopt(this->_serverFD, IPPROTO_TCP, TCP_KEEPINTVL, &intvl,
                 sizeof(intvl))) [[unlikely]] {
    this->_error = true;
    return;
  }
  if (setsockopt(this->_serverFD, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt)))
      [[unlikely]] {
    this->_error = true;
    return;
  }
  this->_sockAddr.sin_family = AF_INET;
  this->_sockAddr.sin_addr.s_addr = INADDR_ANY;
  this->_sockAddr.sin_port = htons(PORT);
  if (bind(this->_serverFD, (struct sockaddr *)&this->_sockAddr,
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

  this->_pool.enqueue(
      std::bind(&FSManager::run, &this->_manager, std::ref(this->_running)));
  std::cout << "Server listening on port " << PORT << '\n';
  if (listen(this->_serverFD, 64) < 0) [[unlikely]] {
    this->_error = true;
    return;
  }
  // socklen_t addrlen = sizeof(this->_sockAddr);
  while (this->_running.load()) {
    if (const int newSocket = accept4(this->_serverFD, nullptr, nullptr, 0);
        newSocket < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }

      perror("Accept");
    } else
      this->_pool.enqueue(std::bind(handleConnection, newSocket,
                                    std::ref(this->_running),
                                    std::ref(this->_manager)));
  }
}

void FSServer::handleConnection(const int fd, const std::atomic_bool &running,
                                FSManager &manager) noexcept {
  Connection con(fd, running, manager);
  if (con)
    con.run();
}

// Connection

FSServer::Connection::Connection(const int fd, const std::atomic_bool &running,
                                 FSManager &manager) noexcept
    : _running(running), _manager(manager) {
  this->_fd = fd;
  this->_private = std::make_unique<RSAKey>(PKEY_PATH, true);
  if (!this->_private || !this->_private->isOk()) {
    std::println("Unable to load private key");
    this->_error = true;
    return;
  }

  struct timeval timeout;
  timeout.tv_sec = 15;
  timeout.tv_usec = 0;
  if (setsockopt(this->_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)))
      [[unlikely]] {
    this->_error = true;
    return;
  }
  if (setsockopt(this->_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)))
      [[unlikely]] {
    this->_error = true;
    return;
  }

  int yes = 1;
  setsockopt(this->_fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));

  int idle = 3;
  int intvl = 5;
  int cnt = 5;

  if (setsockopt(this->_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)))
      [[unlikely]] {
    this->_error = true;
    return;
  }
  if (setsockopt(this->_fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl)))
      [[unlikely]] {
    this->_error = true;
    return;
  }
  if (setsockopt(this->_fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt)))
      [[unlikely]] {
    this->_error = true;
    return;
  }
}

bool FSServer::Connection::interpretCommandRSA(
    const SSLString &command) noexcept {
  if (command._length < COMMAND_LENGTH)
    return false;
  const std::string_view codeView(
      (const char *)(command._data),
      (const char *)(command._data + COMMAND_LENGTH));

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
      (const char *)(command._data),
      (const char *)(command._data + COMMAND_LENGTH));
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
        return sendErrorMessage("Path must be a valid path: " + valOpt.error());
      else
        requests.emplace_back(Request(std::move(file)));
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
      alignas(32) std::array<char, 64> arr;
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
        return sendErrorMessage("Path must be a valid path: " + valOpt.error());
      else
        requests.emplace_back(
            Request(std::move(file), std::move(fsPath), executable[0] == '1'));
    }

    const uint64_t id = this->_manager.addNewNode(std::move(requests));
    alignas(32) const std::array<unsigned char, PTR_SIZE> numberArray =
        ptrToArray(id);
    return sendReply(std::string{numberArray.begin(), numberArray.end()});

  } break;
  case ATTR: {
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
  case LIST: {
    if (dataView.empty())
      return sendErrorMessage("No path was passed");

    if (const auto current = Node::getHeadFile(); !current.has_value()) {
      std::println("Unable to get current head file");
      break;
    } else {
      alignas(32) std::array<char, 64> arr;
      memcpy(arr.data(), current.value().data(), 64);
      LTree tree = LTree(arr, "/FreeSync", true);
      const std::string path(dataView.cbegin(), dataView.cend());
      if (const auto treeOpt = tree.getChildTree(path); !treeOpt.has_value())
        return sendErrorMessage("Unable to find folder");
      else {
        std::string msgRaw = "";
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
  case OK:
    break;

  case QUIT:
    return false;
    break;

  case ERR:
    std::println("Error received");
    return false;
    break;
  case DATA:
    std::println("Shouldn't be receiving a data packet");
    return false;
  case AUTH:
  case AES:
    break;
  }
  return true;
}
void FSServer::Connection::run() noexcept {
  std::println("Initializing validation step");
  if (!handleValidation()) {
    const std::string command = FSCodeStr(QUIT);
    if (!this->writePrimitive(this->_fd, (void *)command.c_str(),
                              command.length()))
      return;
    std::println("Validation step failed");
    return;
  } else {
    const std::string command = FSCodeStr(OK);
    if (!this->writePrimitive(this->_fd, (void *)command.c_str(),
                              command.length()))
      return;
    std::println("Validation step success");
  }

  while (this->_running.load()) {
    if (!this->_aes) {
      if (auto commandOpt = readSocketRSA(this->_fd); !commandOpt.has_value())
        return;
      else {
        const SSLString command = std::move(commandOpt.value());
        if (!interpretCommandRSA(command))
          return;
      }
      // std::cout << command << '\n';
      continue;
    }
    if (StringOpt commandOpt = readSocketAES(this->_fd);
        !commandOpt.has_value())
      return;
    else {
      const SSLString command = std::move(commandOpt.value());
      if (!interpretCommand(command))
        return;
    }
    // std::cout << command << '\n';
    continue;
    // std::cout << command << '\n';
  }
}
