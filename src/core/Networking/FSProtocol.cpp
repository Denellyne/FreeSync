#include "FSProtocol.h"
#include <cassert>
#include <optional>
#include <print>
#include <sys/poll.h>
#include <unistd.h>
#include <vector>

static inline constexpr std::array<unsigned char, LENGTHSIZE>
toArray(unsigned n) {
  std::array<unsigned char, LENGTHSIZE> res{'0'};
  for (int i = 31; i >= 0; i--) {
    res[i] = (n % 10) + '0';
    n /= 10;
  }
  return res;
}
bool FSProtocol::writePrimitive(const int fd, void *data, const uint32_t size) {
  uint32_t idx = 0;
  do {
    const uint32_t res = send(fd, (unsigned char *)data + idx, size - idx, 0);
    if (res > 0)
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

using FSCommand = struct FSProtocol::Command;
bool FSProtocol::writeToSocket(const int fd, const SSLString &data) {
  assert(fd >= 0);
  if (auto blob = this->_pub->encryptBlob(data); blob.has_value()) {
    const SSLString str = blob.value();

    std::array<unsigned char, LENGTHSIZE> sizeArray = toArray(str._length);
    std::vector<unsigned char> packet(sizeArray.begin(), sizeArray.end());
    packet.resize(packet.size() + str._length, 0);
    memcpy(&packet[LENGTHSIZE], str._data, str._length);

    if (!writePrimitive(fd, packet.data(), packet.size())) {
      perror("Unable to send data\n");
      return false;
    }
    return true;
  }

  return false;
}

bool FSProtocol::writeToSocketAES(const int fd, const SSLString &data) {
  assert(fd >= 0);
  if (auto blob = this->_aes->encryptBlob(data); blob.has_value()) {
    const SSLString str = blob.value();

    std::array<unsigned char, LENGTHSIZE> sizeArray = toArray(str._length);
    std::vector<unsigned char> packet(sizeArray.begin(), sizeArray.end());
    packet.resize(packet.size() + str._length, 0);
    memcpy(&packet[LENGTHSIZE], str._data, str._length);

    if (!writePrimitive(fd, packet.data(), packet.size())) {
      perror("Unable to send data\n");
      return false;
    }
    return true;
  }

  return false;
}
StringOpt FSProtocol::readSocketAES(const int fd) {
  assert(this->_private);
  StringOpt strOpt = readSocket(fd);
  if (!strOpt.has_value())
    return std::nullopt;
  if (auto sslOpt = this->_aes->decryptBlob(strOpt.value());
      !sslOpt.has_value())
    return std::nullopt;
  else
    return sslOpt;
}
StringOpt FSProtocol::readSocketRSA(const int fd) {
  assert(this->_private);
  StringOpt strOpt = readSocket(fd);
  if (!strOpt.has_value())
    return std::nullopt;
  if (auto sslOpt = this->_private->decryptBlob(strOpt.value());
      !sslOpt.has_value())
    return std::nullopt;
  else
    return sslOpt;
}

bool FSProtocol::readPacket(const int fd, void *data, const uint32_t numBytes) {
  assert(fd >= 0);
  uint32_t bytesRead = 0;

  memset(data, 0, numBytes);
  do {
    const uint32_t valRead =
        read(fd, (unsigned char *)data + bytesRead, numBytes - bytesRead);
    if (valRead > 0)
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
StringOpt FSProtocol::readSocket(const int fd) {
  assert(fd >= 0);
  constexpr auto toNumber =
      [](const std::array<unsigned char, LENGTHSIZE> &vec) {
        uint32_t num = 0;
        for (const auto c : vec)
          num = (num * 10) + (c - '0');
        return num;
      };
  std::array<unsigned char, LENGTHSIZE> packetSize{'0'};

  if (!readPacket(fd, packetSize.data(), LENGTHSIZE))
    return std::nullopt;
  const uint32_t size = toNumber(packetSize);
  std::vector<unsigned char> data(size, 0);
  if (!readPacket(fd, data.data(), size)) {
    std::println("Unable to read packet\n");
    return std::nullopt;
  }
  return SSLString(data);
}

FSProtocol::CommandQueueOpt FSProtocol::parseCommands(std::string_view input) {
  this->_fragmentBuffer.clear();
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
  std::queue<FSCommand> commands{};
  for (const auto str : split)
    if (!str.empty())
      commands.emplace(Command(str));

  return commands;
}

FSProtocol::Command::Command(std::string_view input) {
  constexpr auto toUpper = [](std::string &str) {
    for (auto &ch : str)
      ch = std::toupper(ch);
  };
  auto split =
      input | std::views::split(std::string_view{" "}) |
      std::views::transform([](auto &&str) { return std::string_view(str); });
  int idx = 0;
  for (auto str : split) {
    if (str.empty())
      continue;
    if (idx == 0)
      this->_command = std::string{str};
    else if (idx == 1)
      this->_arg = std::string{str};
    idx++;
  }
  toUpper(this->_command);
}
