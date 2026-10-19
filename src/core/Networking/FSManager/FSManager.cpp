#include "FSManager.h"
#include "../../Compressor/Compressor.h"
#include "../../Node/LTree.h"
#include "../../Node/Node.h"
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
                     const int backlog) noexcept {
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
FSManager::Connection::processStatus FSManager::Connection::process() noexcept {
  if (!this->_public) {
    if (!this->handleValidation()) {
      std::println("Unable to validate user");
      const std::string command = FSCodeStr(QUIT);
      if (!this->writePrimitive(this->_fd, (void *)command.c_str(),
                                command.length())) {
        std::println("Unable to tell the client to close connection");
        return ERR;
      }
      std::println("Validation step failed");
      return ERR;
    } else {
      const std::string command = FSCodeStr(FSCode::OK);
      if (!this->writePrimitive(this->_fd, (void *)command.c_str(),
                                command.length())) {
        std::println("Unable to confirm validation to client");
        return ERR;
      }
      std::println("Validation step success");
    }
    return OK;
  } else if (!this->_aes) {
    StringOpt opt = readSocketRSA(this->_fd);
    if (!opt.has_value()) {
      std::println("Unable to read AES key");
      return ERR;
    }
    SSLString command = opt.value();
    if (command._length < AES_KEY_BYTES + COMMAND_LENGTH) {
      std::println("Command doesn't have the appropriate length");
      return ERR;
    }
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
    if (const FSCode code = FSStrCode(codeView); code != AESK) {
      std::println("Command isn't AESK");
      return ERR;
    }
    this->_aes = std::make_unique<AESKey>(command._data + COMMAND_LENGTH);
    std::println("Switched to AES");
    return OK;
  } else if (this->_node == 0) {
    if (StringOpt sslOpt = readSocketAES(this->_fd); !sslOpt.has_value()) {
      std::println("Unable to read what node is the client writing to");
      return ERR;
    } else {
      SSLString ssl = sslOpt.value();
      if (auto packetsOpt = ptrToNumber(ssl); !packetsOpt.has_value()) {
        std::println("Packet sent by client isn't a valid node id");
        return ERR;
      } else {
        this->_node = packetsOpt.value();
        return NODESET;
      }
    }
  } else {
    if (StringOpt sslOpt = readSocketAES(this->_fd); !sslOpt.has_value()) {
      std::println("Unable to read from client");
      return ERR;
    } else {
      SSLString ssl = sslOpt.value();
      const std::span<const unsigned char> view{ssl._data, ssl._length};
      if (FSStrCode(std::string_view((char *)view.data(), 3)) == FSCode::OK)
        return FINISHED;

      if (!this->_file.write(view)) {
        std::println("Unable to write to temporary file");
        return ERR;
      }

      return OK;
    }
  }
}
bool FSManager::Connection::confirmFile() noexcept {
  Decompressor decom(this->_file.getFilePath());
  this->_hash = Node::hash(decom.getData());
  if (StringOpt msgOpt = readSocketAES(this->_fd); !msgOpt.has_value()) {
    std::println("Unable to get the client hash, closing connection");
    const SSLString msg(FSCodeStr(FSCode::ERR) +
                        " Unable to get the client hash, closing connection");
    const bool _ = writeToSocketAES(this->_fd, msg);
    return false;
  } else {
    const SSLString msg = msgOpt.value();
    if (msg._length != this->_hash.size()) {
      std::println("Message has different length than our hash");
      const SSLString msg(FSCodeStr(FSCode::ERR) +
                          " Message has different length than our hash");
      const bool _ = writeToSocketAES(this->_fd, msg);
      return false;
    }
    std::span<const unsigned char> dataView =
        std::span<const unsigned char>(msg._data, msg._length);
    if (std::string(dataView.begin(), dataView.end()) != this->_hash) {
      std::println("Hash is not the same");
      const SSLString msg(FSCodeStr(FSCode::ERR) + " Hash is not the same");
      const bool _ = writeToSocketAES(this->_fd, msg);
      return false;
    }
  }
  const SSLString msg(FSCodeStr(FSCode::OK) + " Hash is the same");
  const bool _ = writeToSocketAES(this->_fd, msg);
  return true;
}

bool FSManager::Connection::handleValidation() noexcept {
  if (FILE *fp = fopen(PUBKEY_PATH, "r"); !fp) {
    std::println("Unable to open own public key file");
    return false;
  } else {
    fseek(fp, 0, SEEK_END);
    const long length = ftell(fp);
    std::vector<unsigned char> key(length);
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

bool FSManager::Connection::checkIfAuthorized(
    const std::vector<unsigned char> &key) noexcept {
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

void FSManager::run(const std::atomic_bool &running) noexcept {
  std::vector<std::unique_ptr<FSManager::Connection>> clientList;
  int clientFd = -1;
  struct timeval tv;
  while (running.load()) {
    tv.tv_sec = 0;
    tv.tv_usec = 1000000;
    fd_set readfds, writefds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_SET(this->_serverFD, &readfds);
    FD_SET(this->_serverFD, &writefds);
    int maxfd = this->_serverFD;
    for (const auto &client : clientList) {
      FD_SET(client->getFd(), &readfds);
      FD_SET(client->getFd(), &writefds);
      if (client->getFd() > maxfd)
        maxfd = client->getFd();
    }

    int sd = 0;
    if (sd > maxfd)
      maxfd = sd;

    if (const int activity = select(maxfd + 1, &readfds, &writefds, NULL, &tv);
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

    for (size_t i = 0; i < clientList.size(); i++) {
      sd = clientList[i]->getFd();
      if (FD_ISSET(sd, &readfds) || FD_ISSET(sd, &writefds)) {
        const Connection::processStatus retVal = clientList[i]->process();
        switch (retVal) {
        case Connection::OK:
          break;
        case Connection::NODESET:
          if (auto nodeOpt = this->_list.getNode(clientList[i]->getNode());
              !nodeOpt.has_value()) {
            std::println("Can't find node associated with client");
            if (!removeDeadConnection(clientList[i]->getNode(), sd))
              removeTimeoutConnections();
            clientList.erase(clientList.cbegin() + i);
          } else {
            bool acquired = false;
            auto &nodeData = nodeOpt.value()->_data;
            for (auto &req : nodeData) {
              if (req.id == -1) {
                acquired = true;
                req.id = sd;
                break;
              }
            }
            if (!acquired) {
              if (!removeDeadConnection(clientList[i]->getNode(), sd))
                removeTimeoutConnections();
              clientList.erase(clientList.cbegin() + i);
            }
          }
          break;
        case Connection::FINISHED:
          if (auto nodeOpt = this->_list.getNode(clientList[i]->getNode());
              !nodeOpt.has_value()) {
            std::println("Can't find node associated with client");

            if (!removeDeadConnection(clientList[i]->getNode(), sd))
              removeTimeoutConnections();
            clientList.erase(clientList.cbegin() + i);
          } else {
            std::println("Finished downloading");

            bool allDone = true;
            bool errorFound = false;
            LListPtr<Request> node = nodeOpt.value();
            for (auto &req : node->_data) {
              if (req.id == sd) {
                req.complete = true;
                if (!clientList[i]->confirmFile()) {
                  std::println("Unable to confirm file");
                  errorFound = true;
                  break;
                }
                req.file = std::move(clientList[i]->releaseFile());
                req.hash = clientList[i]->getHash();
              }

              allDone &= req.complete;
            }

            if (allDone && !errorFound) {
              if (const auto current = Node::getHeadFile();
                  !current.has_value()) {
                std::println("Unable to get current head file");
              } else {
                std::array<char, 64> arr;
                memcpy(arr.data(), current.value().data(), 64);
                LTree tree = LTree(arr, "/FreeSync", true);
                for (const auto &req : node->_data) {
                  if (const auto result = tree.addCompressedFile(
                          req.file.toSpan(), req.path, false, req.hash);
                      !result.has_value()) {
                    std::println("Error when adding file:{}", result.error());
                    errorFound = true;
                    break;
                  }
                }

                if (errorFound || !tree.commit())
                  std::println("Unable to commit transaction");
              }
              this->_list.removeNode(this->_list.getId(node).value());
            } else if (!allDone && errorFound)
              this->_list.removeNode(this->_list.getId(node).value());

            clientList.erase(clientList.cbegin() + i);
          }
          break;
        case Connection::ERR:
          std::println("There was an error when processing a client");
          if (!removeDeadConnection(clientList[i]->getNode(), sd))
            removeTimeoutConnections();
          clientList.erase(clientList.cbegin() + i);

          break;
        }
        // std::cout << "client disconnected\n";
        // getpeername(sd, (struct sockaddr *)&this->_serverAddr,
        //             (socklen_t *)&this->_serverAddr);
        // std::cout << "host disconnected, ip: "
        //           << inet_ntoa(this->_serverAddr.sin_addr)
        //           << ", port: " << ntohs(this->_serverAddr.sin_port) <<
        //           "\n";

        // else {
        //   auto &[nodePtr, file] = this->_files[i];
        //   if (std::fwrite(message.data(), sizeof(unsigned char), valread,
        //                   file) < valread)
        //     std::cerr << "Error occurred\n";
        // }
      }
    }
  }
}
