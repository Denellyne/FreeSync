#pragma once
#include "../Encrypt/Encrypt.h"
#include <cstring>
#include <optional>
#include <string>
#include <sys/socket.h>
using StringOpt = std::optional<SSLString>;
#define PORT 20230
#define BUFFERSIZE 512

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

consteval std::string FSCodeStr(const FSCode code) {
  return std::to_string(code);
}

class FSProtocol {
public:
  ~FSProtocol() = default;
  virtual void run() = 0;

protected:
  bool writeToSocket(const int fd, SSLString &message);
  bool writeToSocketAES(const int fd, const std::string_view message);
  // virtual bool writeToSocket(std::string_view message, bool aes = true) = 0;
  // virtual StringOpt readSocket(bool aes = true) = 0;
  StringOpt readSocket(const int fd);
  StringOpt readSocketRSA(const int fd);
  StringOpt readSocketAES(const int fd);
  constexpr void clearBuffer() { memset(this->_buffer.data(), 0, BUFFERSIZE); }

  std::array<unsigned char, BUFFERSIZE> _buffer;
  std::string _currentDir = "/";
  AESPtr _aes = nullptr;
  RSAPtr _pub = nullptr;
  RSAPtr _private = nullptr;
};
