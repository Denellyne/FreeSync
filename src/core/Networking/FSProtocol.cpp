#include "FSProtocol.h"
#include <cassert>
#include <optional>
#include <sys/poll.h>
#include <vector>

bool FSProtocol::writeToSocket(const int fd, SSLString &data) {
  assert(fd >= 0);
  if (auto blob = this->_pub->encryptBlob(data); blob.has_value()) {
    SSLString str = blob.value();
    const ssize_t size = str._length;

    int res = 0;
    int idx = 0;

    do {
      res = send(fd, &str._data[idx], size - idx, 0);
      if (res > 0)
        idx += res;
      else if (res < 0) {
        if (errno == EINTR)
          continue;

        perror("Send");
        return false;
      }
      return false;

    } while (res < size && res > 0);
    return true;
  }

  return false;
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
StringOpt FSProtocol::readSocket(const int fd) {
  assert(fd >= 0);
  std::vector<unsigned char> receivedData;
  int valread = 0;

  do {
    clearBuffer();
    valread = recv(fd, this->_buffer.data(), BUFFERSIZE, 0);
    if (valread > 0) {
      receivedData.insert(receivedData.begin(), this->_buffer.begin(),
                          this->_buffer.begin() + valread);
    } else if (valread == 0)
      break;
    else if (errno == EINTR)
      continue;
    else {
      perror("Recv error");
      return std::nullopt;
    }
  } while (valread < BUFFERSIZE);

  return SSLString(receivedData);
}
