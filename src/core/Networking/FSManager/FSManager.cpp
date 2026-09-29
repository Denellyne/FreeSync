#include "FSManager.h"
#include <arpa/inet.h>
#include <array>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <netinet/tcp.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

FSManager::FSManager(const std::string_view ip, const int port,
                     const int backlog) {
  this->_serverFD = socket(AF_INET, SOCK_STREAM, 0);
  if (0 >= this->_serverFD) {
    std::cerr << "socket creation error\n";
    return;
  }
  if (int opt = 1; setsockopt(this->_serverFD, SOL_SOCKET, SO_REUSEADDR,
                              (char *)&opt, sizeof opt) < 0) {
    std::cerr << "setSocketopt error\n";
    return;
  }

  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(port);
  inet_pton(AF_INET, ip.data(), &serverAddr.sin_addr);

  if (bind(this->_serverFD, (struct sockaddr *)&serverAddr,
           sizeof(serverAddr)) < 0) {
    std::cerr << "bind error\n";
    return;
  }

  if (listen(this->_serverFD, backlog) < 0) {
    std::cerr << "listen error\n";
    return;
  }

  this->_isOk = true;
}
bool FSManager::Connection::process() {
  // if (!this->_public) {
  //   if (!this->handleValidation())
  //     return false;
  //   return true;
  // } else if (!this->_aes) {
  //   StringOpt opt = readSocketRSA(this->_fd);
  //   if (!opt.has_value())
  //     return false;
  //   SSLString command = opt.value();
  //   if (command._length < COMMAND_LENGTH)
  //     return false;
  //   const std::string_view codeView(
  //       (const char *)(command._data),
  //       (const char *)(command._data + COMMAND_LENGTH));
  //   const uint32_t dataViewLength = command._length - COMMAND_LENGTH > 0
  //                                       ? command._length - COMMAND_LENGTH
  //                                       : 0;
  //   std::vector<unsigned char> dataView(dataViewLength);
  //   if (dataViewLength > 0)
  //     memcpy(dataView.data(), command._data + COMMAND_LENGTH,
  //            command._length - COMMAND_LENGTH);
  //   const FSCode code = FSStrCode(codeView);
  //   if (code != AESK)
  //     return false;
  //   if (command._length < AES_KEY_BYTES + COMMAND_LENGTH)
  //     return false;
  //   this->_aes = std::make_unique<AESKey>(command._data + COMMAND_LENGTH);
  //   return true;
  // }

  constexpr unsigned bufsize = 1024;
  std::array<unsigned char, bufsize> message;
  bool x = readPacket(this->_fd, message.data(), bufsize);
  if (!x) {
    std::cout << "a\n";
    return false;
  }
  std::cout << "b\n";

  return true;
}
bool FSManager::Connection::handleValidation() {
  std::vector<unsigned char> key;
  std::array<unsigned char, LENGTH_SIZE> lengthArr;
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

bool FSManager::Connection::checkIfAuthorized(
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

void FSManager::run(const std::atomic_bool &running) {
  std::vector<std::unique_ptr<FSManager::Connection>> clientList;
  int clientFd = -1;
  struct timeval tv;
  while (running.load()) {
    tv.tv_sec = 0;
    tv.tv_usec = 1000000;
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(this->_serverFD, &readfds);
    int maxfd = this->_serverFD;
    for (const auto &client : clientList) {
      FD_SET(client->getFd(), &readfds);
      if (client->getFd() > maxfd)
        maxfd = client->getFd();
    }

    int sd = 0;
    if (sd > maxfd)
      maxfd = sd;

    if (const int activity = select(maxfd + 1, &readfds, NULL, NULL, &tv);
        activity < 0) {
      std::cerr << "select error\n";
      continue;
    }
    if (FD_ISSET(this->_serverFD, &readfds)) {
      clientFd = accept(this->_serverFD, (struct sockaddr *)NULL, NULL);
      if (clientFd < 0) {
        continue;
      }

      clientList.emplace_back(
          std::make_unique<FSManager::Connection>(clientFd));

      // std::cout << "new client connected\n";
      // std::cout << "new connection, socket fd is " << clientFd
      //           << ", ip is: " << inet_ntoa(this->_serverAddr.sin_addr)
      //           << ", port: " << ntohs(this->_serverAddr.sin_port) << "\n";
    }

    for (int i = 0; i < clientList.size(); i++) {
      constexpr unsigned bufsize = 1024;
      sd = clientList[i]->getFd();
      if (FD_ISSET(sd, &readfds)) {
        if (!clientList[i]->process()) {
          // std::cout << "client disconnected\n";
          // getpeername(sd, (struct sockaddr *)&this->_serverAddr,
          //             (socklen_t *)&this->_serverAddr);
          // std::cout << "host disconnected, ip: "
          //           << inet_ntoa(this->_serverAddr.sin_addr)
          //           << ", port: " << ntohs(this->_serverAddr.sin_port) <<
          //           "\n";

          close(sd);
          clientList.erase(clientList.cbegin() + i);
          removeDeadConnection(clientList[i]->getNode());
        } // else {
        //   auto &[nodePtr, file] = this->_files[i];
        //   if (std::fwrite(message.data(), sizeof(unsigned char), valread,
        //                   file) < valread)
        //     std::cerr << "Error occurred\n";
        // }
      }
    }
  }
}
