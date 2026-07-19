#pragma once
#include "../Encrypt/Encrypt.h"
#include <cstring>
#include <optional>
#include <queue>
#include <string>
#include <sys/socket.h>
using StringOpt = std::optional<SSLString>;
#define PORT 20230
#define BUFFERSIZE 512
#define LENGTHSIZE 32

enum FSCode {
  ERR = 100,  // Generic Error command followed by reason
  OK = 200,   // Generic OK Command
  LIST = 201, // Start of list of current directory
  DATA = 202, // Generic Data Command, eg: SIZE(301) file.txt -> DATA(202) 123
  TRNF = 203, // Start of blob
  RDY = 204,  // Specifies the User is ready to start the action, You might want
              // to ask for the size of the file before starting to download
  AESK = 205, // Gives the AES-256 key to the other User
  OKIV = 206, // Can be used in the same places as the OK but it also
              // invalidates the AES Key, as such the next message sent to the
              // Server needs to be an AESK(205)

  AUTH = 300, // Asks for Public Key of Client
  SIZE = 301, // Asks for file size
  RETR = 302, // Asks to download file in current directory, RETR file.txt
  STRU = 303, // Asks to store file in current directory, STRU file.txt
  PWD = 304,  // Asks what the current directory is, PWD(304) -> DATA(202)
              // /FreeSync
  CWD = 305,  // Asks to change to the specified directory
  DEL = 306,  // Asks to delete file
  ATTR = 307, // Asks the attributes of a specific file
  AES = 308,  // Asks the User to generate a new AES key to be used for the
              // session, either after a set ammount of times the key is used or
              // for a transfer, there only exists one AES key at a time
};
consteval int FSStrCode(const std::string_view code) {
  if (code == "OK")
    return 200;
  else if (code == "LIST")
    return 201;
  else if (code == "DATA")
    return 202;
  else if (code == "TRNF")
    return 203;
  else if (code == "RDY")
    return 204;
  else if (code == "AESK")
    return 205;
  else if (code == "OKIV")
    return 206;
  else if (code == "RDY")
    return 204;
  else if (code == "AUTH")
    return 300;
  else if (code == "SIZE")
    return 301;
  else if (code == "RETR")
    return 302;
  else if (code == "STRU")
    return 303;
  else if (code == "PWD")
    return 304;
  else if (code == "CWD")
    return 305;
  else if (code == "DEL")
    return 306;
  else if (code == "ATTR")
    return 307;
  else if (code == "AES")
    return 308;

  return 100;
}

consteval std::string FSCodeStr(const FSCode code) {
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

  bool writeToSocket(const int fd, SSLString &message);
  bool writeToSocketAES(const int fd, const std::string_view message);
  // virtual bool writeToSocket(std::string_view message, bool aes = true) = 0;
  // virtual StringOpt readSocket(bool aes = true) = 0;
  constexpr bool readPacket(const int fd, void *data, uint32_t numBytes);
  StringOpt readSocket(const int fd);
  StringOpt readSocketRSA(const int fd);
  StringOpt readSocketAES(const int fd);
  constexpr void clearBuffer() { memset(this->_buffer.data(), 0, BUFFERSIZE); }

  std::array<unsigned char, BUFFERSIZE> _buffer;
  std::string _currentDir = "/";
  AESPtr _aes = nullptr;
  RSAPtr _pub = nullptr;
  RSAPtr _private = nullptr;
  std::string _fragmentBuffer = "";
};
