#pragma once
#include "../DiskObject/DiskFile.hpp"
#include "../Encrypt/Encrypt.h"
#include <chrono>
#include <cstring>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <type_traits>
using StringOpt = std::optional<SSLString>;
#define PORT 20230
#define FSMANAGER_PORT 20231
#define BUFFER_SIZE 512
#define VALIDATION_LENGTH 48
#define COMMAND_LENGTH 3
#define PUBKEY_PATH "certs/pub.pem"
#define PKEY_PATH "certs/priv.pem"
#define CLIENTS_CERTS_PATH "clients/"
#define FSMANAGER_PACKET 64 * 1024

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
  // PUBK = 306, // Sends the public key

};
constexpr std::string FSPrint(const FSCode code) noexcept {

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
  // else if (code == PUBK)
  //   return "PUBK";

  return "ERR";
}

constexpr FSCode FSStrCode(const std::string_view command) noexcept {
  if (command.length() < 3 || (command.length() > 3 && command[3] != ' '))
    return ERR;

  const std::string_view code = command.substr(0, command.find_first_of(' '));
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
  // else if (code == "306")
  //   return PUBK;

  return ERR;
}

constexpr std::string FSCodeStr(const FSCode code) noexcept {
  return std::to_string(code);
}

struct Request {
  enum OP { ADD, DELETE } type = ADD;
  Request(const std::string_view filePath, const std::string_view fsPath,
          const bool isExecutable, const Request::OP t) noexcept
      : type(t), filePath(filePath), fsPath(fsPath),
        isExecutable(isExecutable) {}
  Request(const Request &) = delete;
  Request &operator=(const Request &) = delete;

  Request(Request &&) noexcept = default;
  Request &operator=(Request &&other) noexcept {
    std::swap(this->filePath, other.filePath);
    std::swap(this->fsPath, other.fsPath);
    std::swap(this->id, other.id);
    std::swap(this->complete, other.complete);
    std::swap(this->type, other.type);
    std::swap(this->file, other.file);
    return *this;
  }
  std::string filePath = "", fsPath = "", hash = "";
  int id = -1;
  bool complete = type == DELETE, isExecutable = false;
  DiskFile file = DiskFile(true);
  const int64_t timestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
};
class FSProtocol {
public:
  ~FSProtocol() = default;
  virtual void run() = 0;

protected:
  [[nodiscard]] bool writeToSocket(const int fd,
                                   const SSLString &message) noexcept;
  [[nodiscard]] bool writeToSocketAES(const int fd,
                                      const SSLString &message) noexcept;
  [[nodiscard]] bool writePrimitive(const int fd, void *data,
                                    const uint32_t size) noexcept;
  // virtual bool writeToSocket(std::string_view message, bool aes = true) = 0;
  // virtual StringOpt readSocket(bool aes = true) = 0;
  [[nodiscard]] bool readPacket(const int fd, void *data,
                                uint32_t numBytes) noexcept;
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
  RSAPtr _private = nullptr;
  RSAPtr _public = nullptr;
  // std::string _fragmentBuffer = "";
};
