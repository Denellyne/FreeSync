#pragma once
#include <atomic>
#include <netinet/in.h>
#include <print>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#define PORT 21
#define BUFFERSIZE 8192

class FTP {
public:
  FTP(std::atomic_bool &running);
  ~FTP() {
    std::println("Terminating FTP Server...");
    if (this->_serverFD < 0)
      return;
    do
      close(this->_serverFD);
    while (errno == EAGAIN || errno == EWOULDBLOCK);
    this->_serverFD = -1;
  }

  void run();
  static void handleConnection(const int fd, const std::atomic_bool &running);
  constexpr static bool isUserValid(const std::string_view user,
                                    const std::string_view pass) {
    return FTP::_users[user.data()] == pass;
  }

  struct Connection {
    Connection() = delete;
    Connection(const int fd, bool &valid);
    ~Connection() {
      std::println("Closing connection of sock:{}", this->_fd);
      closeSocket(const_cast<int &>(this->_fd));
      closeSocket(this->_dataSock);
    }

    void run(const std::atomic_bool &running);

  private:
    int acceptSocket(const int &sock, const sockaddr_in &sockAddr);
    int readSocket();
    int writeToSocket(const int fd, std::string_view &message);
    bool write(std::string message);
    bool writeDataSocket(const std::string message, const int fd);
    bool handleLogin();
    bool passiveMode();
    void closeSocket(int &fd) {
      if (fd < 0)
        return;
      do
        close(fd);
      while (errno == EAGAIN || errno == EWOULDBLOCK);
      fd = -1;
    }

    const int _fd;
    int _dataSock = -1;
    struct sockaddr_in _dataAddr;
    std::string currentPath = "/FreeSync";
    char _buffer[BUFFERSIZE];
  };

private:
  static std::unordered_map<std::string, std::string> _users;
  struct sockaddr_in _sockAddr;
  int _serverFD;
  std::atomic_bool &_running;
};
