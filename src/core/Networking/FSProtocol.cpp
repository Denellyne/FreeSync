#include "FSProtocol.h"
#include <cassert>
#include <optional>
#include <print>
#include <sys/poll.h>
#include <unistd.h>
#include <vector>

bool FSProtocol::writePrimitive(const int fd, void *data,
                                const uint32_t size) noexcept {
  uint32_t idx = 0;
  do {
    const ssize_t res = send(fd, (unsigned char *)data + idx, size - idx, 0);
    if (res > 0) [[likely]]
      idx += res;
    else if (res < 0) {
      if (errno == EINTR)
        continue;

      perror("Send");
      return false;
    }
  } while (idx < size);

  return true;
}

bool FSProtocol::writeToSocket(const int fd, const SSLString &data) noexcept {
  assert(fd >= 0 && this->_public && this->_private &&
         !this->_public->_isPrivateKey);
  if (auto blob = this->_public->encryptBlob(data); blob.has_value())
      [[likely]] {
    const SSLString str = std::move(blob.value());

    alignas(32) std::array<unsigned char, LENGTH_SIZE> sizeArray =
        toArray(str._length);
    std::vector<unsigned char> packet(sizeArray.begin(), sizeArray.end());
    packet.resize(packet.size() + str._length, 0);
    memcpy(&packet[LENGTH_SIZE], str._data, str._length);
    auto sigOpt = this->_private->signBlob(data);
    if (!sigOpt.has_value()) [[unlikely]]
      return false;
    const SSLString sig = std::move(sigOpt.value());
    const size_t packetLength = packet.size();
    packet.resize(packet.size() + sig._length, 0);
    memcpy(&packet[packetLength], sig._data, sig._length);

    if (!writePrimitive(fd, packet.data(), packet.size())) [[unlikely]] {
      perror("Unable to send data\n");
      return false;
    }
    return true;
  }

  return false;
}

bool FSProtocol::writeToSocketAES(const int fd,
                                  const SSLString &data) noexcept {
  assert(fd >= 0);
  if (this->_aes == nullptr) [[unlikely]] {
    std::println("No AES Key available");
    return false;
  }
  if (auto blob = this->_aes->encryptBlob(data); blob.has_value()) [[likely]] {
    const SSLString str = std::move(blob.value());

    alignas(32) std::array<unsigned char, LENGTH_SIZE> sizeArray =
        toArray(str._length);
    std::vector<unsigned char> packet(sizeArray.begin(), sizeArray.end());
    packet.resize(packet.size() + str._length, 0);
    memcpy(&packet[LENGTH_SIZE], str._data, str._length);
    auto sigOpt = this->_private->signBlob(data);
    if (!sigOpt.has_value()) [[unlikely]]
      return false;
    const SSLString sig = std::move(sigOpt.value());
    const size_t packetLength = packet.size();
    packet.resize(packet.size() + sig._length, 0);
    memcpy(&packet[packetLength], sig._data, sig._length);

    if (!writePrimitive(fd, packet.data(), packet.size())) [[unlikely]] {
      perror("Unable to send data\n");
      return false;
    }
    return true;
  }

  return false;
}
StringOpt FSProtocol::readSocketAES(const int fd) noexcept {
  assert(fd >= 0);
  StringOpt strOpt = readSocket(fd);
  if (!strOpt.has_value()) [[unlikely]]
    return std::nullopt;
  alignas(32) std::vector<unsigned char> sig(RSA_SIGNING_LENGTH);
  if (!readPacket(fd, sig.data(), RSA_SIGNING_LENGTH)) [[unlikely]]
    return std::nullopt;

  if (auto sslOpt = this->_aes->decryptBlob(strOpt.value());
      !sslOpt.has_value()) [[unlikely]]
    return std::nullopt;
  else {
    SSLString ssl = std::move(sslOpt.value());
    const SSLString signature = SSLString(sig);
    if (!this->_public->validateBlob(ssl, signature)) [[unlikely]]
      return std::nullopt;
    return ssl;
  }
}
StringOpt FSProtocol::readSocketRSA(const int fd) noexcept {
  assert(fd >= 0);
  assert(this->_private->_isPrivateKey);
  StringOpt strOpt = readSocket(fd);
  if (!strOpt.has_value()) [[unlikely]]
    return std::nullopt;
  alignas(32) std::vector<unsigned char> sig(RSA_SIGNING_LENGTH);
  if (!readPacket(fd, sig.data(), RSA_SIGNING_LENGTH)) [[unlikely]]
    return std::nullopt;

  if (auto sslOpt = this->_private->decryptBlob(strOpt.value());
      !sslOpt.has_value()) [[unlikely]]
    return std::nullopt;
  else {
    SSLString ssl = std::move(sslOpt.value());
    const SSLString signature = SSLString(sig);
    if (!this->_public->validateBlob(ssl, signature)) [[unlikely]]
      return std::nullopt;
    return ssl;
  }
}

bool FSProtocol::readPacket(const int fd, void *data,
                            const uint32_t numBytes) noexcept {
  assert(fd >= 0);
  uint32_t bytesRead = 0;

  memset(data, 0, numBytes);
  do {
    const uint32_t valRead =
        read(fd, (unsigned char *)data + bytesRead, numBytes - bytesRead);
    if (valRead > 0) [[likely]]
      bytesRead += valRead;
    else if (valRead == 0)
      return false;
    else {
      if (errno == EINTR)
        continue;
      perror("Read error");
      return false;
    }
  } while (numBytes > bytesRead);
  return true;
}
StringOpt FSProtocol::readSocket(const int fd) noexcept {
  assert(fd >= 0);
  alignas(32) std::array<unsigned char, LENGTH_SIZE> packetSize{'0'};

  if (!readPacket(fd, packetSize.data(), LENGTH_SIZE)) [[unlikely]]
    return std::nullopt;
  if (const auto sizeOpt = toNumber(packetSize); !sizeOpt.has_value())
      [[unlikely]]
    return std::nullopt;
  else {
    const uint32_t size = sizeOpt.value();
    std::vector<unsigned char> data(size, 0);
    if (!readPacket(fd, data.data(), size)) [[unlikely]] {
      std::println("Unable to read packet\n");
      return std::nullopt;
    }
    return SSLString(data);
  }
}
