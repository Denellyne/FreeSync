#include "FSServer.h"
#include "../../Node/LTree.h"
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/random.h>
#include <thread>

FSServer::FSServer(std::atomic_bool &running) : _running(running) {
  int opt = 1;
  struct timeval timeout;
  timeout.tv_sec = 10;
  timeout.tv_usec = 0;
  if ((this->_serverFD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
    throw std::runtime_error("Socket failed\n");

  if (setsockopt(this->_serverFD, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt)))
    throw std::runtime_error("Failed to set socket options\n");
  if (setsockopt(this->_serverFD, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                 sizeof(timeout)))
    throw std::runtime_error("Failed to set socket options\n");
  if (setsockopt(this->_serverFD, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                 sizeof(timeout)))
    throw std::runtime_error("Failed to set socket options\n");

  int yes = 1;
  setsockopt(this->_serverFD, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));

  int idle = 120;
  int intvl = 30;
  int cnt = 5;

  if (setsockopt(this->_serverFD, IPPROTO_TCP, TCP_KEEPIDLE, &idle,
                 sizeof(idle)))
    throw std::runtime_error("TCP_KEEPIDLE error\n");
  if (setsockopt(this->_serverFD, IPPROTO_TCP, TCP_KEEPINTVL, &intvl,
                 sizeof(intvl)))
    throw std::runtime_error("TCP_KEEPINTVL error\n");
  if (setsockopt(this->_serverFD, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt)))
    throw std::runtime_error("TCP_KEEPCNT error\n");
  this->_sockAddr.sin_family = AF_INET;
  this->_sockAddr.sin_addr.s_addr = INADDR_ANY;
  this->_sockAddr.sin_port = htons(PORT);
  if (bind(this->_serverFD, (struct sockaddr *)&this->_sockAddr,
           sizeof(this->_sockAddr)) < 0)
    throw std::runtime_error("Failed to bind\n");
}

void FSServer::run() {
  std::cout << "Server listening on port " << PORT << '\n';
  if (listen(this->_serverFD, 64) < 0)
    throw std::runtime_error("Failed to listen to incoming connections\n");
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
      this->_pool.enqueue(
          std::bind(handleConnection, newSocket, std::ref(this->_running)));
  }
}

void FSServer::handleConnection(const int fd, const std::atomic_bool &running) {
  bool valid = true;
  Connection con(fd, running, valid);
  if (valid)
    con.run();
}

// Connection

FSServer::Connection::Connection(const int fd, const std::atomic_bool &running,
                                 bool &valid)
    : _fd(fd), _running(running) {
  this->_private = std::make_unique<RSAKey>(PKEY_PATH, true);
  if (!this->_private) {
    std::println("Unable to load private key");
    valid = false;
  }

  struct timeval timeout;
  timeout.tv_sec = 60 * 15;
  timeout.tv_usec = 0;
  if (setsockopt(this->_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)))
    throw std::runtime_error("Failed to set socket options\n");
  if (setsockopt(this->_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)))
    throw std::runtime_error("Failed to set socket options\n");

  int yes = 1;
  setsockopt(this->_fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));

  int idle = 120;
  int intvl = 30;
  int cnt = 5;

  if (setsockopt(this->_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)))
    throw std::runtime_error("TCP_KEEPIDLE error\n");
  if (setsockopt(this->_fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl)))
    throw std::runtime_error("TCP_KEEPINTVL error\n");
  if (setsockopt(this->_fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt)))
    throw std::runtime_error("TCP_KEEPCNT error\n");
}

bool FSServer::Connection::checkIfAuthorized(
    const std::vector<unsigned char> &key) {
  for (const auto &file :
       std::filesystem::directory_iterator(CLIENTS_CERTS_PATH)) {
    if (file.is_directory())
      continue;

    if (int fd = open(file.path().c_str(), O_RDONLY, 0); fd < 0) {
      std::cerr << "Unable to open file " << file << "\n";
      return false;
    } else {
      unsigned char *buf = (unsigned char *)mmap(
          NULL, file.file_size(), PROT_READ, MAP_FILE | MAP_SHARED, fd, 0);
      if (key.size() == file.file_size() &&
          !memcmp(key.data(), buf, key.size())) {
        munmap(buf, file.file_size());
        close(fd);
        return true;
      }
      munmap(buf, file.file_size());
      close(fd);
    }
  }
  return false;
}

bool FSServer::Connection::handleValidation() {
  // const std::string str = generateRandomString();
  if (FILE *fp = fopen(PUBKEY_PATH, "r"); !fp) {
    std::println("Unable to open own public key file");
    return false;
  } else {
    fseek(fp, 0, SEEK_END);
    const long length = ftell(fp);
    std::vector<unsigned char> key(length);
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
    key.clear();
    if (!readPacket(this->_fd, lengthArr.data(), LENGTH_SIZE)) {
      std::println("Unable to read key length");
      return false;
    }
    auto clientKeyLengthOpt = toNumber(lengthArr);
    if (!clientKeyLengthOpt.has_value()) {
      std::println("Unable to get key length");
      return false;
    }
    const long clientKeyLength = clientKeyLengthOpt.value();
    key.resize(clientKeyLength);
    if (!readPacket(this->_fd, key.data(), clientKeyLength)) {
      std::println("Unable to read key");
      return false;
    }
    if (!checkIfAuthorized(key)) {
      std::println("Key is not authorized");
      return false;
    }
    this->_public = std::make_unique<RSAKey>(key);
    if (!this->_public) {
      std::println("Unable to load public key");
      return false;
    }

    return true;
  }
}

// std::string FSServer::Connection::generateRandomString() {
//
//   std::string res = "";
//   res.resize(VALIDATION_LENGTH + 1);
//   res[VALIDATION_LENGTH] = '\0';
//   ssize_t idx = 0;
//   while (idx < VALIDATION_LENGTH) {
//     const ssize_t bytes = getrandom(&res[idx], VALIDATION_LENGTH, 0);
//     if (bytes < 0)
//       throw std::runtime_error("Unable to generatate random string");
//     idx += bytes;
//   }
//   return res;
// }

bool FSServer::Connection::interpretCommand(const SSLString &command) {
  if (command._length < COMMAND_LENGTH)
    return false;
  const std::string_view codeView(
      (const char *)(command._data),
      (const char *)(command._data + COMMAND_LENGTH));
  const uint32_t dataViewLength = command._length - COMMAND_LENGTH > 0
                                      ? command._length - COMMAND_LENGTH
                                      : 0;
  std::vector<unsigned char> dataView(dataViewLength);
  if (dataViewLength > 0)
    memcpy(dataView.data(), command._data + COMMAND_LENGTH,
           command._length - COMMAND_LENGTH);
  const FSCode code = FSStrCode(codeView);
  switch (code) {
  case DEL: {
  } break;
  case RETR: {
  } break;
  case STRU: {
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
    if (const auto current = Node::getHeadFile(); !current.has_value()) {
      std::println("Unable to get current head file");
      break;
    } else {
      std::array<char, 64> arr;
      memcpy(arr.data(), current.value().data(), 64);
      LTree tree = LTree(arr, "/FreeSync", true);
      const std::string_view path(
          (const char *)(command._data + COMMAND_LENGTH),
          (const char *)(command._data + command._length));
      if (const auto treeOpt = tree.getChildTree(path); !treeOpt.has_value()) {
        const SSLString msg(FSCodeStr(ERR) + " Unable to find folder");
        return writeToSocketAES(this->_fd, msg);
      } else {
        std::string msgRaw = "";
        for (const auto &child : treeOpt.value().getChildren()) {
          std::string entry = "File - ";
          std::string fileSize = "0 - ";
          if (child._entry == DIRECTORY)
            entry = "Directory - ";
          else {
            fileSize = std::to_string(child.getFileSize()) + " - ";
            if (child._entry != REGULAR_FILE)
              entry = "Executable - ";
          }
          msgRaw += entry + fileSize + child._fileName + '\n';
        }

        const SSLString msg(FSCodeStr(OK) + ' ' + msgRaw);
        return writeToSocketAES(this->_fd, msg);
      }
    }

  } break;
  case OK:
    break;

  case QUIT:
    return false;
    break;

  default:
  case ERR:
    std::println("Error received");
    return false;
    break;
  }
  return true;
}
void FSServer::Connection::run() {
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
      if (const auto commandOpt = readSocketRSA(this->_fd);
          !commandOpt.has_value())
        return;
      else {
        const SSLString command = commandOpt.value();
        if (!interpretCommand(command))
          return;
      }
      // std::cout << command << '\n';
      continue;
    }
    if (const auto commandOpt = readSocketAES(this->_fd);
        !commandOpt.has_value())
      return;
    else {
      const SSLString command = commandOpt.value();
      if (!interpretCommand(command))
        return;
    }
    // std::cout << command << '\n';
    continue;
    // std::cout << command << '\n';
  }
}
