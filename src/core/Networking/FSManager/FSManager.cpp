#include "FSManager.h"
#include <arpa/inet.h>
#include <array>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

template <typename T>
FSManager<T>::FSManager(const std::string_view ip, const int port,
                        const int backlog) {

  this->_serverfd = socket(AF_INET, SOCK_STREAM, 0);
  if (0 >= this->_serverfd) {
    std::cerr << "socket creation error\n";
    return;
  }
  if (int opt = 1; setsockopt(this->_serverfd, SOL_SOCKET, SO_REUSEADDR,
                              (char *)&opt, sizeof opt) < 0) {
    std::cerr << "setSocketopt error\n";
    return;
  }

  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(port);
  inet_pton(AF_INET, ip.data(), &serverAddr.sin_addr);

  if (bind(this->_serverfd, (struct sockaddr *)&serverAddr,
           sizeof(serverAddr)) < 0) {
    std::cerr << "bind error\n";
    return;
  }

  if (listen(this->_serverfd, backlog) < 0) {
    std::cerr << "listen error\n";
    return;
  }

  this->_private = std::make_shared<RSAKey>(PKEY_PATH, true);
  if (!this->_private) {
    std::println("Unable to load private key");
    return;
  }
  this->_isOk = true;
}

template <typename T> bool FSManager<T>::Connection::handleValidation() {
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

template <typename T>
bool FSManager<T>::Connection::checkIfAuthorized(
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
template <typename T> void FSManager<T>::run(const std::atomic_bool &running) {
  std::vector<FSManager::Connection> clientList;
  int clientFd = -1;
  while (running.load()) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(this->_serverfd, &readfds);
    int maxfd = this->_serverfd;
    for (const auto sd : clientList) {
      FD_SET(sd, &readfds);
      if (sd > maxfd)
        maxfd = sd;
    }

    int sd = 0;
    if (sd > maxfd)
      maxfd = sd;

    if (const int activity = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        activity < 0) {
      // std::cerr << "select error\n";
      continue;
    }
    if (FD_ISSET(this->_serverfd, &readfds)) {
      clientFd = accept(this->_serverfd, (struct sockaddr *)NULL, NULL);
      if (clientFd < 0) {
        // std::cerr << "accept error\n";
        continue;
      }

      clientList.emplace_back(clientFd);
      // std::cout << "new client connected\n";
      // std::cout << "new connection, socket fd is " << clientFd
      //           << ", ip is: " << inet_ntoa(this->_serverAddr.sin_addr)
      //           << ", port: " << ntohs(this->_serverAddr.sin_port) << "\n";
    }

    for (int i = 0; i < clientList.size(); i++) {
      constexpr unsigned bufsize = 1024;
      std::array<unsigned char, bufsize> message;
      sd = clientList[i];
      if (FD_ISSET(sd, &readfds)) {
        if (const size_t valread = read(sd, message.data(), bufsize);
            valread == 0) {
          // std::cout << "client disconnected\n";

          getpeername(sd, (struct sockaddr *)&this->_serverAddr,
                      (socklen_t *)&this->_serverAddr);
          // std::cout << "host disconnected, ip: "
          //           << inet_ntoa(this->_serverAddr.sin_addr)
          //           << ", port: " << ntohs(this->_serverAddr.sin_port) <<
          //           "\n";
          close(sd);
          this->_files.erase(i);
          clientList.erase(clientList.cbegin() + i);
        } else {
          auto &[nodePtr, file] = this->_files[i];
          if (std::fwrite(message.data(), sizeof(unsigned char), valread,
                          file) < valread)
            std::cerr << "Error occurred\n";
        }
      }
    }
  }
}
