#pragma once
#include "../FSProtocol.h"
#include <print>
class FSConnection : public FSProtocol {
public:
  FSConnection() = default;
  operator bool() const noexcept { return !this->_error; }

protected:
  inline bool sendErrorMessage(const std::string_view err) noexcept {
    const SSLString msg(FSCodeStr(ERR) + ' ' + err.data());
    return writeToSocketAES(this->_fd, msg);
  }
  inline bool sendReply(const std::string_view message) noexcept {
    const SSLString msg(FSCodeStr(OK) + ' ' + message.data());
    return writeToSocketAES(this->_fd, msg);
  }
  bool handleValidation() noexcept {
    if (FILE *fp = fopen(PUBKEY_PATH, "r"); !fp) {
      std::println("Unable to open own public key file");
      return false;
    } else {
      fseek(fp, 0, SEEK_END);
      const long length = ftell(fp);
      std::vector<unsigned char> key(length);
      rewind(fp);
      if (fread(key.data(), sizeof(unsigned char), length, fp) <
          (size_t)length) {
        std::println("Unable to read own public key");
        fclose(fp);
        return false;
      }
      fclose(fp);
      alignas(32) std::array<unsigned char, LENGTH_SIZE> lengthArr =
          toArray(length);
      key.insert(key.begin(), lengthArr.begin(), lengthArr.end());
      if (!writePrimitive(this->_fd, key.data(), LENGTH_SIZE + length)) {
        std::println("Unable to send primitive string");
        return false;
      }
      key.clear();
      if (!readPacket(this->_fd, lengthArr.data(), LENGTH_SIZE)) {
        std::println("Unable to read key length");
        return false;
      }
      auto clientKeyLengthOpt = toNumber(lengthArr);
      if (!clientKeyLengthOpt.has_value()) {
        std::println("Unable to get key length");
        return false;
      }
      const long clientKeyLength = clientKeyLengthOpt.value();
      key.resize(clientKeyLength);
      if (!readPacket(this->_fd, key.data(), clientKeyLength)) {
        std::println("Unable to read key");
        return false;
      }
      if (!checkIfAuthorized(key)) {
        std::println("Key is not authorized");
        return false;
      }
      this->_public = std::make_unique<RSAKey>(key);
      if (!this->_public || !this->_public->isOk()) {
        std::println("Unable to load public key");
        return false;
      }

      return true;
    }
  }
  static bool checkIfAuthorized(const std::span<unsigned char> key) noexcept {
    if (key.size() > 512)
      return false;

    for (const auto &file :
         std::filesystem::directory_iterator(CLIENTS_CERTS_PATH)) {
      if (file.is_directory())
        continue;

      if (int fd = open(file.path().c_str(), O_RDONLY, 0); fd < 0) {
        std::cerr << "Unable to open file " << file << "\n";
        return false;
      } else {
        unsigned char *buf = (unsigned char *)mmap(
            NULL, file.file_size(), PROT_READ, MAP_FILE | MAP_SHARED, fd, 0);
        if (key.size() == file.file_size() &&
            !memcmp(key.data(), buf, key.size())) {
          munmap(buf, file.file_size());
          close(fd);
          return true;
        }
        munmap(buf, file.file_size());
        close(fd);
      }
    }
    return false;
  }

  int _fd = -1;
  bool _error = false;
};
