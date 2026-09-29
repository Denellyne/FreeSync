#pragma once
#include "../../FSLList/FSLList.h"
#include "../../TempFile/TempFile.hpp"
#include "../FSProtocol.h"
#include <atomic>
#include <netinet/in.h>
#include <string_view>
#include <unistd.h>
class FSManager {
public:
  FSManager(const std::string_view ip = "localhost",
            const int port = FSMANAGER_PORT, const int backlog = 128);
  void run(const std::atomic_bool &running);
  uintptr_t addNewNode(std::vector<Request> &requests);
  void removeDeadConnection(const uintptr_t connection) {
    if (connection < 0)
      return;
    auto nodeOpt = this->_list.getNode(connection);
    if (!nodeOpt.has_value())
      return;
    auto node = nodeOpt.value();
    bool remove = false;
    for (const auto &r : node->_data) {
      if (r.complete == false && r.id == -1) {
        remove = true;
        break;
      }
    }
    if (remove)
      this->_list.removeNode(connection);
  }

private:
  void validateList();
  class Connection final : public FSProtocol {
  public:
    Connection() = delete;
    Connection(const int fd) : _fd(fd) {
      this->_private = std::make_unique<RSAKey>(PKEY_PATH, true);
      if (!this->_private) {
        std::println("Unable to load private key");
        this->_fd = -1;
      }
    }
    ~Connection() {
      std::println("Closing connection of sock:{}", this->_fd);
      close(const_cast<int &>(this->_fd));
    }
    bool process();
    virtual void run() override {}
    constexpr int getFd() const { return this->_fd; }
    constexpr uintptr_t getNode() const { return this->_node; }
    constexpr void setNodeId(const uintptr_t nodeId) { this->_node = nodeId; }

  private:
    // std::string generateRandomString();
    bool handleValidation();
    bool handleAES();
    bool interpretCommand(const SSLString &command);
    bool checkIfAuthorized(const std::vector<unsigned char> &key);

    int _fd = -1;
    uintptr_t _node = 0;
    TempFile _file;
  };
  FSLList<Request> _list;
  struct sockaddr_in _serverAddr;
  int _serverFD = -1;
  bool _isOk = false;
};
