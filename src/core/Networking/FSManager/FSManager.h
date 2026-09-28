#pragma once
#include "../../FSLList/FSLList.h"
#include "../../TempFile/TempFile.hpp"
#include "../FSProtocol.h"
#include <atomic>
#include <netinet/in.h>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
template <typename T> class FSManager {
public:
  FSManager(const std::string_view ip, const int port, const int backlog = 128);
  FSLList<T> &getList() { return this->_list; }
  void run(const std::atomic_bool &running);

private:
  class Connection final : public FSProtocol {
  public:
    Connection() = delete;
    Connection(const int fd);
    ~Connection() {
      std::println("Closing connection of sock:{}", this->_fd);
      close(const_cast<int &>(this->_fd));
    }
    bool process(RSASPtr &privateKey);

  private:
    // std::string generateRandomString();
    bool handleValidation();
    bool handleAES();
    bool interpretCommand(const SSLString &command);
    bool checkIfAuthorized(const std::vector<unsigned char> &key);

    int _fd = -1;
  };
  FSLList<T> _list;
  std::unordered_map<int, std::tuple<uintptr_t, TempFile>> _files;
  struct sockaddr_in _serverAddr;
  int _serverfd = -1;
  RSASPtr _private = nullptr;
  bool _isOk = false;
};
