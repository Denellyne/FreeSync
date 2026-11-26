#include "FSClientI.h"
#include "../../Compressor/Compressor.h"
#include "../../Node/Node.h"
#include "../../defines.hpp"

bool FSClientI::connect() noexcept {
  if (this->checkConnection())
    return true;

  if (this->_fd >= 0)
    close(this->_fd);

  this->_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (this->_fd < 0) [[unlikely]] {
    std::println("Creating the socket failed {}", this->_fd);
    return false;
  }

  const int connStatus = ::connect(
      this->_fd, reinterpret_cast<struct sockaddr *>(&this->_serverAddr),
      sizeof(this->_serverAddr));
  if (connStatus < 0) [[unlikely]] {
    std::println("There was an error making a connection to the server {}",
                 connStatus);
    return false;
  }
  if (!this->validationStep()) [[unlikely]] {
    std::println("There was an error making a connection to the server, "
                 "validation step ");
    return false;
  }
  if (!this->switchAES()) [[unlikely]] {
    std::println("There was an error while switching to AES");
    return false;
  }
  return true;
}

constexpr bool FSClientI::checkConnection() noexcept {
  if (this->_fd < 0 || !this->_aes.get() || !this->_aes->isOk()) [[unlikely]]
    return false;
  const SSLString packet = SSLString(FSCodeStr(PING));
  return this->writeToSocketAES(this->_fd, packet);
}
bool FSClientI::switchAES() noexcept {
  this->_aes = std::make_unique<AESKey>();
  if (!this->_aes || !this->_aes->isOk()) {
    std::println("Unable to generate AES key");
    return false;
  }
  this->_aesLifetime = 1;
  const SSLString packet =
      FSClientI::generatePacket(AESK, this->_aes->getKey());
  std::println("Packet generated");
  if (!this->writeToSocket(this->_fd, packet)) {
    std::println("Unable to write AESKey to server");
    return false;
  }
  std::println("Switched to AES encryption");
  return true; // Check if received OK
}

bool FSClientI::issueCommand(const SSLString &command) noexcept {
  return this->writeToSocketAES(this->_fd, command);
}

std::optional<SSLString>
FSClientI::issueCommandRead(const SSLString &command) noexcept {
  if (!issueCommand(command)) [[unlikely]]
    return std::nullopt;

  return readSocketAES(this->_fd);
}
bool FSClientI::finishDownload(DiskFile &&file,
                               const std::string_view outPath) noexcept {

  if (StringOpt msgOpt = readSocketAES(this->_fd); !msgOpt.has_value()) {
    std::println("Unable to get the server hash, closing connection");
    return false;
  } else {
    if (!file)
      return false;
    const SSLString msg = std::move(msgOpt.value());
    file = decompressData(file.toSpan());
    if (!file) {
      std::println("Unable to decompress file");
      return false;
    }
    const std::string hash = Node::hash(file.toSpan());
    if (msg._length != hash.size()) {
      std::println("Message has different length than our hash");
      return false;
    }
    std::span<const unsigned char> dataView =
        std::span<const unsigned char>(msg._data, msg._length);
    if (std::string(dataView.begin(), dataView.end()) != hash) {
      std::println("Hash is not the same");
      return false;
    }

    DiskFile out(outPath, true);
    if (!out)
      std::println("Unable to create file: {} {}", std::strerror(errno),
                   out.getFilePath());

    return out.write(file.toSpan());
  }
  std::println("Finished receiving file");
  return true;
}

bool FSClientI::downloadFile(const std::string_view outPath) noexcept {
  DiskFile file;
  if (!file)
    std::println("Unable to create file: {} {}", std::strerror(errno),
                 file.getFilePath());

  while (true) {
    if (StringOpt sslOpt = readSocketAES(this->_fd); !sslOpt.has_value()) {
      std::println("Unable to read from server");
      return false;
    } else {
      SSLString ssl = std::move(sslOpt.value());
      const std::span<const unsigned char> view{ssl._data, ssl._length};
      if (view.size() < 3) {
        std::println("Buffer is too small, an error occured");
        continue;
      }

      if (FSStrCode(std::string_view((char *)view.data(), 3)) == FSCode::OK)
        return this->finishDownload(std::move(file), outPath);
      else if (FSStrCode(std::string_view((char *)view.data(), 3)) ==
               FSCode::ERR) {
        const std::string err = std::string(view.begin() + 3, view.end());
        std::println("Error from server:{}", err);
        return false;
      } else if (FSStrCode(std::string_view((char *)view.data(), 3)) ==
                     FSCode::DATA &&
                 file && !file.write(view.subspan(3))) {
        std::println("Unable to write to temporary file");
        return false;
      }
    }
  }
}

bool FSClientI::validationStep() noexcept {

  alignas(ALIGNMENT) std::array<unsigned char, LENGTH_SIZE> serverKeyLength;
  if (!readPacket(this->_fd, serverKeyLength.data(), LENGTH_SIZE)) {
    std::println("Unable to read serverKeyLength");
    return false;
  }
  auto clientKeyLengthOpt = toNumber(serverKeyLength);
  if (!clientKeyLengthOpt.has_value()) {
    std::println("Unable to get key length");
    return false;
  }
  const size_t clientKeyLength = clientKeyLengthOpt.value();
  if (clientKeyLength > 1024) {
    std::println("Key is to large to be a RSA Key");
    return false;
  }

  std::vector<unsigned char> key(clientKeyLength);
  if (!readPacket(this->_fd, key.data(), clientKeyLength)) {
    std::println("Unable to read key");
    return false;
  }
  this->_public = std::make_unique<RSAKey>(key);
  if (!this->_public || !this->_public->isOk()) {
    std::println("Unable to load public key");
    return false;
  }
  if (FILE *fp = fopen(getCanonicalPath(PUBKEY_PATH).data(), "r");
      fp == nullptr) {
    std::println("Unable to open own public key file");
    return false;
  } else {
    fseek(fp, 0, SEEK_END);
    const size_t length = ftell(fp);
    key.resize(length);
    rewind(fp);
    if (fread(key.data(), sizeof(unsigned char), length, fp) < length) {
      std::println("Unable to read own public key");
      fclose(fp);
      return false;
    }
    fclose(fp);
    alignas(ALIGNMENT) std::array<unsigned char, LENGTH_SIZE> lengthArr =
        toArray(length);
    key.insert(key.begin(), lengthArr.begin(), lengthArr.end());
    if (!writePrimitive(this->_fd, key.data(), LENGTH_SIZE + length)) {
      std::println("Unable to send primitive string");
      return false;
    }
  }
  std::array<unsigned char, COMMAND_LENGTH> buf;
  if (!readPacket(this->_fd, buf.data(), COMMAND_LENGTH))
    return false;
  else {
    const std::string command(buf.begin(), buf.begin() + COMMAND_LENGTH);
    if (FSStrCode(command) == QUIT) {
      std::println("Closing connection, failed to validate it");
      return false;
    }
  }
  return true;
}

bool FSClientI::invalidateAES() noexcept {
  assert(this->_aes);
  const SSLString packet = FSClientI::generatePacket(INV, this->_aes->getKey());
  std::println("Packet generated");
  if (!this->writeToSocketAES(this->_fd, packet)) {
    std::println("Unable to write AESKey to server");
    return false;
  }
  return true;
}
