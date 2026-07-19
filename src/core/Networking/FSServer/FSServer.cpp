#include "FSServer.h"
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
  socklen_t addrlen = sizeof(this->_sockAddr);
  while (this->_running.load()) {
    if (const int newSocket = accept4(
            this->_serverFD, (struct sockaddr *)&this->_sockAddr, &addrlen, 0);
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

void FSServer::Connection::Connection::run() {
  while (this->_running.load()) {
    SSLString enc = readSocket(this->_fd).value();
    std::cout << enc << '\n';
    SSLString str = this->_private->decryptBlob(enc).value();

    std::cout << str << '\0' << '\n';
    // std::println("{}", enc);
  }
}
