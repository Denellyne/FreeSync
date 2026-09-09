#pragma once
#include "../Encrypt/Encrypt.h"
#include <concepts>
#include <cstring>
#include <optional>
#include <queue>
#include <string>
#include <sys/socket.h>
#include <type_traits>
using StringOpt = std::optional<SSLString>;
#define PORT 20230
#define BUFFER_SIZE 512
#define VALIDATION_LENGTH 48
#define COMMAND_LENGTH 3

enum FSCode {
  ERR = 100,  // Generic Error command followed by reason
  QUIT = 101, // Generic close connection command
  OK = 200,   // Generic OK Command may contain message
  LIST = 201, // Start of list of current directory
  AESK = 202, // Gives the AES-256 key to the other User
  INV = 203,  // Invalidates the aes encryption
  AUTH = 300, // Asks for Public Key of Client
  RETR = 301, // Asks to download file in current directory, RETR file.txt
  STRU = 302, // Asks to store file in current directory, STRU file.txt
  DEL = 303,  // Asks to delete file
  ATTR = 304, // Asks the attributes of a specific file
  AES = 305,  // Asks the User to generate a new AES key to be used for the
              // session, either after a set ammount of times the key is used or
              // for a transfer, there only exists one AES key at a time

};
constexpr std::string FSPrint(const FSCode code) {

  if (code == OK)
    return "OK";
  else if (code == QUIT)
    return "QUIT";
  else if (code == LIST)
    return "LIST";
  else if (code == AESK)
    return "AESK";
  else if (code == INV)
    return "INV";
  else if (code == AUTH)
    return "AUTH";
  else if (code == RETR)
    return "RETR";
  else if (code == STRU)
    return "STRU";
  else if (code == DEL)
    return "DEL";
  else if (code == ATTR)
    return "ATTR";
  else if (code == AES)
    return "AES";

  return "ERR";
}

constexpr FSCode FSStrCode(const std::string_view command) {
  const std::string_view code = command.substr(0, command.find(' '));
  if (code == "200")
    return OK;
  else if (code == "101")
    return QUIT;
  else if (code == "201")
    return LIST;
  else if (code == "202")
    return AESK;
  else if (code == "203")
    return INV;
  else if (code == "300")
    return AUTH;
  else if (code == "301")
    return RETR;
  else if (code == "302")
    return STRU;
  else if (code == "303")
    return DEL;
  else if (code == "304")
    return ATTR;
  else if (code == "305")
    return AES;

  return ERR;
}

constexpr std::string FSCodeStr(const FSCode code) {
  return std::to_string(code);
}

class FSProtocol {
public:
  ~FSProtocol() = default;
  virtual void run() = 0;

  struct Command {
    Command() = delete;
    Command(std::string_view input);

    std::string _command, _arg;
  };

  using CommandQueue = std::queue<FSProtocol::Command>;
  using CommandQueueOpt = std::optional<CommandQueue>;

protected:
  CommandQueueOpt parseCommands(std::string_view input);
  Command Command(std::string_view input);

  [[nodiscard]] bool writeToSocket(const int fd, const SSLString &message);
  [[nodiscard]] bool writeToSocketAES(const int fd, const SSLString &message);
  [[nodiscard]] bool writePrimitive(const int fd, void *data,
                                    const uint32_t size);
  // virtual bool writeToSocket(std::string_view message, bool aes = true) = 0;
  // virtual StringOpt readSocket(bool aes = true) = 0;
  [[nodiscard]] bool readPacket(const int fd, void *data, uint32_t numBytes);
  [[nodiscard]] StringOpt readSocket(const int fd);
  [[nodiscard]] StringOpt readSocketRSA(const int fd);
  [[nodiscard]] StringOpt readSocketAES(const int fd);
  // constexpr void clearBuffer() { memset(this->_buffer.data(), 0,
  // BUFFER_SIZE); }
  template <ByteSpan... Args>
  SSLString generatePacket(FSCode code, Args... arg) {
    uint32_t size = COMMAND_LENGTH;
    for (const auto &vec : {arg...})
      size += vec.size();

    std::vector<unsigned char> packet(size);
    {
      const std::string command = FSCodeStr(code);
      memcpy(packet.data(), command.c_str(), COMMAND_LENGTH);
    }

    uint32_t idx = COMMAND_LENGTH;
    for (const auto &vec : {arg...}) {
      memcpy(packet.data() + idx, vec.data(), vec.size());
      idx += vec.size();
    }

    return SSLString(packet);
  }

  // std::array<unsigned char, BUFFER_SIZE> _buffer;
  // std::string _currentDir = "/";
  AESPtr _aes = nullptr;
  RSAPtr _pub = nullptr;
  RSAPtr _private = nullptr;
  // std::string _fragmentBuffer = "";
};
