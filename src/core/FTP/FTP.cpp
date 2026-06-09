#include "FTP.h"
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <cstring>
#include <iostream>
#include <optional>
#include <ranges>
#include <sys/poll.h>
#include <sys/socket.h>
#include <thread>
using CommandVector = std::vector<FTP::Connection::Command>;
using CommandVectorOpt = std::optional<CommandVector>;
using FTPCommand = FTP::Connection::Command;

std::unordered_map<std::string, std::string> FTP::_users = {
    {"Santos", "fedora"}};
FTP::FTP(std::atomic_bool &running) : _running(running) {
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
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }

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

StringOpt FTP::Connection::readSocket() {

  std::string receivedData = "";
  int valread = 0;
  struct pollfd pfd;
  pfd.fd = this->_fd;
  pfd.events = POLLIN;

  int pollResult = poll(&pfd, 1, SOCKET_TIMEOUT);

  if (pollResult < 0) {
    if (errno == EINTR)
      return readSocket();
    perror("Poll error");
    return std::nullopt;
  } else if (pollResult == 0) {
    std::println("Client socket timed out waiting for input.");
    return std::nullopt;
  }

  do {
    memset(this->_buffer, 0, BUFFERSIZE);
    valread = recv(this->_fd, this->_buffer, BUFFERSIZE, 0);
    if (valread > 0) {
      receivedData.append(this->_buffer, valread);
    } else if (valread == 0)
      break;
    else if (errno == EAGAIN || errno == EWOULDBLOCK)
      break;
    else if (errno == EINTR)
      continue;
    else {
      perror("Recv error");
      return std::nullopt;
    }
  } while (valread != 0);

  std::println("Received: {}", receivedData);
  return receivedData;
}

int FTP::Connection::writeToSocket(const int fd, std::string_view &message) {
  const ssize_t size = message.length();
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
  const ssize_t size = message.length();
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
  const ssize_t size = view.size();
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
  bool isValid = true;
  std::string user = "";
  std::string pass = "";
  while (isValid) {
    if (!user.empty() && !pass.empty())
      return FTP::isUserValid(user, pass);

    StringOpt opt = readSocket();
    if (!opt.has_value())
      return false;
    CommandVectorOpt vecOpt = parseCommands(opt.value());
    if (!vecOpt.has_value())
      return false;
    CommandVector commands = vecOpt.value();
    for (const auto &command : commands) {
      if (!isValid)
        return false;
      if (!user.empty() && !pass.empty())
        return FTP::isUserValid(user, pass);

      else if (checkCommand(command._command, "CLNT")) {
        std::cout << command._arg << " Connected to server\n";
        isValid &= write("200 Command okay");
      } else if (checkCommand(command._command, "AUTH TLS"))
        isValid &= isValid & write("534 TLS not supported");
      else if (checkCommand(command._command, "AUTH SSL"))
        isValid &= write("534 SSL not supported");
      else if (checkCommand(command._command, "USER")) {
        user = command._arg;
        isValid &= write("331 Password required.");
      } else if (checkCommand(command._command, "PASS"))
        pass = command._arg;
      else
        isValid &=
            write("202 Command not implemented, superfluous at this site");
    }
  }
  return isValid;
}
bool FTP::Connection::passiveMode() {
  closeSocket(this->_dataSock);
  if ((this->_dataSock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0) {
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

int FTP::Connection::acceptSocket(const int &sock,
                                  const sockaddr_in &sockAddr) {
  if (sock < 0)
    return -1;

  struct pollfd pfd;
  pfd.fd = sock;
  pfd.events = POLLIN;

  int pollResult = poll(&pfd, 1, SOCKET_TIMEOUT);

  if (pollResult < 0) {
    if (errno == EINTR)
      return acceptSocket(sock, sockAddr);
    perror("Poll error");
    return false;
  } else if (pollResult == 0) {
    std::println("Client socket timed out waiting to accept.");
    return false;
  }
  socklen_t addrlen = sizeof(sockAddr);
  if (int newSocket =
          accept4(sock, (struct sockaddr *)&sockAddr, &addrlen, SOCK_NONBLOCK);
      newSocket < 0) {
    if (errno == EINTR)
      return acceptSocket(sock, sockAddr);

    perror("Accept");
    return -1;
  } else
    return newSocket;
}
void FTP::Connection::run(const std::atomic_bool &running) {

  bool connectionValid = true;
  while (connectionValid && running.load()) {
    StringOpt opt = readSocket();
    if (!opt.has_value())
      break;
    const std::string input = this->_fragmentBuffer + opt.value();
    CommandVectorOpt vecOpt = parseCommands(input);
    if (!vecOpt.has_value())
      break;
    CommandVector commands = vecOpt.value();
    for (const auto &command : commands) {

      if (checkCommand(command._command, "QUIT")) {
        connectionValid &= write("221 Closing connection");
        connectionValid = false;
        break;
      } else if (checkCommand(command._command, "SYST"))
        connectionValid &= write("215 UNIX Type: L8");
      else if (checkCommand(command._command, "FEAT"))
        connectionValid &=
            write("211-Extensions supported:\r\n UTF8\r\n211 End");
      else if (checkCommand(command._command, "TYPE"))
        connectionValid &= write("200 TYPE set");
      else if (checkCommand(command._command, "NOOP"))
        connectionValid &= write("200 Command OK");
      else if (checkCommand(command._command, "CWD")) {
        this->_currentPath = command._arg;
        connectionValid &= write("250 Directory Changed");
      } else if (checkCommand(command._command, "STRU"))
        if (checkCommand(command._arg, "F"))
          connectionValid &= write("200 Command OK");
        else
          connectionValid &=
              write("504 Command not implemented for that parameter");
      else if (checkCommand(command._command, "MODE")) {
        if (checkCommand(command._arg, "S"))
          connectionValid &= write("200 Command OK");
        else
          connectionValid &=
              write("504 Command not implemented for that parameter");
      }

      else if (checkCommand(command._command, "SIZE"))
        connectionValid &= write("213 123");

      else if (checkCommand(command._command, "MDTM"))
        connectionValid &= write("213 20260609214600");

      else if (checkCommand(command._command, "RETR")) {
        if (this->_dataSock < 0) {
          connectionValid &= write("425 Use PASV first");
          continue;
        }

        connectionValid &= write("150 Opening binary mode data connection");
        int newSocket = this->acceptSocket(this->_dataSock, this->_dataAddr);

        if (newSocket == -1) {
          connectionValid &= write("226 Couldn't accept new socket");
          closeSocket(this->_dataSock);
          continue;
        }
        connectionValid &= writeDataSocket(
            "TEST STRING OF ME TRANSFERING SHIT MUAHSHAHA poookie +E "
            "MUTIO TOTO E CHEIRA A CU HEHEHHE PARA DEOLHAR PARA AQUI "
            "SUA TOTO   ",
            newSocket);
        closeSocket(newSocket);
        closeSocket(this->_dataSock);
        connectionValid &= write("226 Transfer Complete");
      } else if (checkCommand(command._command, "PWD")) {
        const std::string message = "257 \"" + this->_currentPath + "\"";
        connectionValid &= write(message);
      } else if (checkCommand(command._command, "PASV")) {
        if (!this->passiveMode()) {
          connectionValid &= write("500 Unable to enter Passive Mode");
          continue;
        }
        socklen_t len = sizeof(this->_dataAddr);
        getsockname(this->_dataSock, (sockaddr *)&this->_dataAddr, &len);

        const int port = ntohs(this->_dataAddr.sin_port);
        sockaddr_in localAddr;
        socklen_t addrLen = sizeof(localAddr);
        getsockname(this->_fd, (struct sockaddr *)&localAddr, &addrLen);

        std::string ipStr = inet_ntoa(localAddr.sin_addr);
        std::replace(ipStr.begin(), ipStr.end(), '.', ',');
        const int p1 = port / 256;
        const int p2 = port % 256;
        const std::string message = "227 Entering Passive Mode (" + ipStr +
                                    ',' + std::to_string(p1) + ',' +
                                    std::to_string(p2) + ')';
        connectionValid &= write(message);
      } else if (checkCommand(command._command, "LIST")) {
        if (this->_dataSock < 0) {
          connectionValid &= write("425 Use PASV first");
          continue;
        }

        int newSocket = this->acceptSocket(this->_dataSock, this->_dataAddr);

        if (newSocket == -1) {
          connectionValid &= write("226 Couldn't accept new socket");
          closeSocket(this->_dataSock);
          continue;
        }

        connectionValid &= write("150 Directory listing");
        connectionValid &= writeDataSocket(
            "-rw-r--r-- 1 user group 123 Jan 01 12:00 file.txt\r\n", newSocket);
        closeSocket(newSocket);
        closeSocket(this->_dataSock);
        connectionValid &= write("226 Transfer Complete");
      } else
        connectionValid &=
            write("202 Command not implemented, superfluous at this site");
    }
  }
}
CommandVectorOpt FTP::Connection::parseCommands(std::string_view input) {
  if (!input.ends_with("\r\n")) {
    const size_t idx = input.rfind("\r\n");
    if (idx != input.npos) {
      this->_fragmentBuffer = std::string{input.substr(idx + 2)};
      input = input.substr(0, idx + 2);
    } else {
      std::cerr << "Incomplete input passed\n";
      return std::nullopt;
    }
  }

  auto split =
      input | std::views::split(std::string_view{"\r\n"}) |
      std::views::transform([](auto &&str) { return std::string_view(str); });
  std::vector<FTPCommand> commands{};
  commands.reserve(std::ranges::distance(split));
  for (const auto str : split)
    if (!str.empty())
      commands.emplace_back(FTPCommand(str));

  return commands;
}

FTP::Connection::Command::Command(std::string_view input) {
  constexpr auto toUpper = [](std::string &str) {
    for (auto &ch : str)
      ch = std::toupper(ch);
  };
  auto split =
      input | std::views::split(std::string_view{" "}) |
      std::views::transform([](auto &&str) { return std::string_view(str); });
  int idx = 0;
  for (auto str : split) {
    if (idx == 0)
      this->_command = std::string{str};
    else if (idx == 1)
      this->_arg = std::string{str};
    idx++;
  }
  toUpper(this->_command);
}
