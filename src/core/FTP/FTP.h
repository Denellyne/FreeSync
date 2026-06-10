#pragma once
#include <algorithm>
#include <atomic>
#include <iostream>
#include <netinet/in.h>
#include <print>
#include <queue>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#define PORT 21
#define BUFFERSIZE 8192
#define SOCKET_TIMEOUT 30000

using StringOpt = std::optional<std::string>;

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
    return FTP::_users[user.data()] == pass.data();
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

    struct Command {
      Command() = delete;
      Command(std::string_view input);

      std::string _command, _arg;
    };

    using CommandQueue = std::queue<FTP::Connection::Command>;
    using CommandQueueOpt = std::optional<CommandQueue>;

  private:
    std::optional<CommandQueue> parseCommands(std::string_view input);
    int acceptSocket(const int &sock, const sockaddr_in &sockAddr);
    StringOpt readSocket();
    int writeToSocket(const int fd, std::string_view &message);
    bool write(std::string message);
    bool writeDataSocket(const std::string message, const int fd);
    bool handleLogin(CommandQueue &queue, std::string &user, std::string &pass);
    bool passiveMode();
    bool checkCommand(const std::string_view buffer,
                      const std::string_view command) {
      return buffer == command;
    }

    void closeSocket(int &fd) {
      if (fd < 0)
        return;
      do {
      } while (close(fd) == -1 && errno == EINTR);
      fd = -1;
    }

    std::string _fragmentBuffer = "";
    const int _fd;
    int _dataSock = -1;
    struct sockaddr_in _dataAddr;
    std::string _currentPath = "/";
    char _buffer[BUFFERSIZE];
  };

private:
  static std::unordered_map<std::string, std::string> _users;
  struct sockaddr_in _sockAddr;
  int _serverFD = -1;
  std::atomic_bool &_running;
};
