#include "FTP.h"
#include <asm-generic/socket.h>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <thread>

FTP::FTP(std::atomic_bool &running) : _running(running) {
  int opt = 1;
  struct timeval timeout;
  timeout.tv_sec = 10;
  timeout.tv_usec = 0;
  if ((this->_serverFD = socket(AF_INET, SOCK_STREAM, 0)) == 0)
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

  this->_sockAddr.sin_family = AF_INET;
  this->_sockAddr.sin_addr.s_addr = INADDR_ANY;
  this->_sockAddr.sin_port = htons(PORT);
  if (bind(this->_serverFD, (struct sockaddr *)&this->_sockAddr,
           sizeof(this->_sockAddr)) < 0)
    throw std::runtime_error("Failed to bind\n");
}

void FTP::run() {
  std::cout << "Server listening on port " << PORT << '\n';
  if (listen(this->_serverFD, 64) < 0)
    throw std::runtime_error("Failed to listen to incoming connections\n");
  socklen_t addrlen = sizeof(this->_sockAddr);
  while (this->_running.load()) {
    if (const int newSocket =
            accept4(this->_serverFD, (struct sockaddr *)&this->_sockAddr,
                    &addrlen, SOCK_NONBLOCK);
        newSocket < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        continue;

      perror("Accept");
    } else
      std::thread(handleConnection, newSocket, std::ref(this->_running))
          .detach();
  }
}
void FTP::handleConnection(const int fd, const std::atomic_bool &running) {
  bool valid = true;
  Connection con(fd, valid);
  if (!valid || !running.load())
    return;
  return con.run(running);
}

FTP::Connection::Connection(const int fd, bool &valid) : _fd(fd) {

  valid = write("220 Welcome");
  valid &= handleLogin();

  if (!valid)
    write("431 Log-on unsuccessful. User and/or password invalid.");
  else
    write("230 User is logged in, may proceed.");
}

int FTP::Connection::readSocket() {

  memset(this->_buffer, 0, BUFFERSIZE);
  int res = 0;

  do {
    const int valread =
        recv(this->_fd, &this->_buffer[res], BUFFERSIZE - res, 0);
    if (valread > 0)
      res += valread;
    else if (valread == 0) {
      perror("Recv");
      return 0;
    } else if (errno == EAGAIN || errno == EWOULDBLOCK)
      continue;

    std::cout << "Received: " << this->_buffer << '\n';
  } while (this->_buffer[res - 2] != '\r' && this->_buffer[res - 1] != '\n');

  return res;
}

int FTP::Connection::writeToSocket(const int fd, std::string_view &message) {
  const size_t size = message.length();
  const int res = send(fd, message.data(), size, 0);
  if (res > 0)
    return res;
  else if (res < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return 0;

    perror("Send");
    return -1;
  }
  return -1;
}

bool FTP::Connection::write(std::string message) {
  message += "\r\n";
  const size_t size = message.length();
  ssize_t sent = 0;
  std::string_view view = message;
  while (sent < size) {
    const ssize_t val = this->writeToSocket(this->_fd, view);
    if (val == -1)
      return false;
    view.remove_prefix(val);
    sent += val;
  }
  std::cout << "Wrote " << message;
  return true;
}

bool FTP::Connection::writeDataSocket(const std::string message, const int fd) {
  if (this->_dataSock < 0 || fd < 0)
    return false;
  ssize_t sent = 0;
  std::string_view view = message;
  const size_t size = view.size();
  while (sent < size) {
    const ssize_t val = this->writeToSocket(fd, view);
    if (val == -1)
      return false;
    view.remove_prefix(val);
    sent += val;
  }
  std::cout << "Wrote " << message;
  return true;
}
bool FTP::Connection::handleLogin() {

  ssize_t valread = readSocket();
  if (!valread)
    return false;

  if (strstr(this->_buffer, "AUTH TLS") != NULL)
    write("534 TLS not supported");

  valread = readSocket();
  if (!valread)
    return false;
  if (strstr(this->_buffer, "AUTH SSL") != NULL)
    write("534 SSL not supported");

  valread = readSocket();
  if (!valread)
    return false;
  if (strstr(this->_buffer, "USER") != NULL) {
    std::string user = this->_buffer;
    user.erase(user.length() - 2, 2);
    user.erase(0, 5);
    if (user != "Santos")
      return false;
    write("331 Password required.");
  } else
    return false;
  valread = readSocket();
  if (!valread)
    return false;
  if (strstr(this->_buffer, "PASS") != NULL) {
    std::string pass = this->_buffer;
    pass.erase(pass.length() - 2, 2);
    pass.erase(0, 5);
    if (pass != "fedora")
      return false;
  } else
    return false;

  return true;
}
bool FTP::Connection::passiveMode() {
  int opt = 1;
  if (this->_dataSock != -1)
    close(this->_dataSock);
  if ((this->_dataSock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) ==
      0) {
    std::cerr << "Unable to open socket for data\n";
    return false;
  }

  this->_dataAddr.sin_family = AF_INET;
  this->_dataAddr.sin_addr.s_addr = INADDR_ANY;
  this->_dataAddr.sin_port = 0;
  if (bind(this->_dataSock, (struct sockaddr *)&this->_dataAddr,
           sizeof(this->_dataAddr)) < 0) {
    std::cerr << "Unable to bind socket for data\n";
    return false;
  }
  if (listen(this->_dataSock, 1) < 0) {
    std::cerr << "Unable to begin listening on data socket\n";
    return false;
  }
  return true;
}
void FTP::Connection::run(const std::atomic_bool &running) {

  // send(con._fd, "220 Features: a\r\n", 17, 0);
  bool connectionValid = true;
  while (connectionValid && running.load()) {
    connectionValid = readSocket();
    if (!connectionValid)
      break;

    if (strstr(this->_buffer, "QUIT") != NULL) {
      connectionValid &= write("221 Closing connection");
      break;
    } else if (strstr(this->_buffer, "TYPE") != NULL)
      connectionValid &= write("200 TYPE set");

    else if (strstr(this->_buffer, "CWD") != NULL)
      connectionValid &= write("250 Directory Changed");
    else if (strstr(this->_buffer, "RETR") != NULL) {
      if (this->_dataSock < 0) {
        connectionValid &= write("425 Use PASV first");
        continue;
      }

      connectionValid &= write("150 Opening binary mode data connection");
    retry:
      int newSocket;
      socklen_t addrlen = sizeof(this->_dataAddr);
      if ((newSocket =
               accept4(this->_dataSock, (struct sockaddr *)&this->_dataAddr,
                       &addrlen, SOCK_NONBLOCK)) < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
          goto retry;
        perror("Accept");

        connectionValid &= write("226 Couldn't accept new socket");
        close(this->_dataSock);
        this->_dataSock = -1;
        continue;
      }
      connectionValid &= writeDataSocket(
          "TEST STRING OF ME TRANSFERING SHIT MUAHSHAHA poookie +E "
          "MUTIO TOTO E CHEIRA A CU HEHEHHE PARA DEOLHAR PARA AQUI "
          "SUA TOTO   ",
          newSocket);
      close(newSocket);
      close(this->_dataSock);
      this->_dataSock = -1;
      connectionValid &= write("226 Transfer Complete");
    } else if (strstr(this->_buffer, "PWD") != NULL) {
      const std::string message = "257 \"" + this->currentPath + "\"";
      connectionValid &= write(message);
    } else if (strstr(this->_buffer, "PASV") != NULL) {
      if (!this->passiveMode()) {
        connectionValid &= write("500 Unable to enter Passive Mode");
        continue;
      }
      socklen_t len = sizeof(this->_dataAddr);
      getsockname(this->_dataSock, (sockaddr *)&this->_dataAddr, &len);

      const int port = ntohs(this->_dataAddr.sin_port);

      const int p1 = port / 256;
      const int p2 = port % 256;
      const std::string message = "227 Entering Passive Mode (127,0,0,1," +
                                  std::to_string(p1) + ',' +
                                  std::to_string(p2) + ')';
      connectionValid &= write(message);
    } else if (strstr(this->_buffer, "LIST") != NULL) {
      if (this->_dataSock < 0) {
        connectionValid &= write("425 Use PASV first");
        continue;
      }

    retry2:
      int newSocket;
      socklen_t addrlen = sizeof(this->_dataAddr);
      if ((newSocket =
               accept4(this->_dataSock, (struct sockaddr *)&this->_dataAddr,
                       &addrlen, SOCK_NONBLOCK)) < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
          goto retry2;
        perror("Accept");
        connectionValid &= write("226 Couldn't accept new socket");

        do
          close(this->_dataSock);
        while (errno == EAGAIN || errno == EWOULDBLOCK);

        this->_dataSock = -1;
        continue;
      }

      connectionValid &= write("150 Directory listing");
      connectionValid &= writeDataSocket(
          "-rw-r--r-- 1 user group 123 Jan 01 12:00 file.txt\r\n", newSocket);
      do
        close(newSocket);
      while (errno == EAGAIN || errno == EWOULDBLOCK);

      do
        close(this->_dataSock);
      while (errno == EAGAIN || errno == EWOULDBLOCK);
      this->_dataSock = -1;
      connectionValid &= write("226 Transfer Complete");
    } else
      connectionValid &=
          write("202 Command not implemented, superfluous at this site");
  }
}
