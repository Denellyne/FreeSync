#pragma once
#include <atomic>
#include <netinet/in.h>
#include <print>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define PORT 21
#define BUFFERSIZE 8192

class FTP {
public:
  FTP(std::atomic_bool &running);
  ~FTP() {
    if (this->_serverFD != -1)
      close(this->_serverFD);
  }

  void run();
  static void handleConnection(const int fd, const std::atomic_bool &running);

  struct Connection {
    const int _fd;
    int _dataSock = -1;
    struct sockaddr_in _dataAddr;
    std::string currentPath = "/FreeSync";
    char _buffer[BUFFERSIZE];
    Connection() = delete;
    Connection(const int fd, bool &valid);
    ~Connection() {
      std::println("Closing connection of sock:{}", this->_fd);
      close(this->_fd);
      if (this->_dataSock != -1)
        close(this->_dataSock);
    }

    void run(const std::atomic_bool &running);

  private:
    int readSocket();
    int writeToSocket(const int fd, std::string_view &message);
    bool write(std::string message);
    bool writeDataSocket(const std::string message, const int fd);
    bool handleLogin();
    bool passiveMode();
  };
  struct sockaddr_in _sockAddr;
  int _serverFD;
  std::atomic_bool &_running;
};
