#pragma once
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <span>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

class TempFile {
public:
  TempFile() {
    char TMP_TEMPLATE[] = "/tmp/FS_XXXXXXXXX";
    this->_fd = mkstemp(TMP_TEMPLATE);
    this->_filePath = TMP_TEMPLATE;
    unlink(TMP_TEMPLATE);
  }
  TempFile(const TempFile &other) = delete;
  TempFile(TempFile &other) = delete;
  TempFile &operator=(const TempFile &other) = delete;
  TempFile &operator=(TempFile &&other) noexcept {
    std::swap(this->_fd, other._fd);
    return *this;
  }
  TempFile(TempFile &&other) noexcept : _fd(std::exchange(other._fd, -1)) {}
  ~TempFile() noexcept {
    if (this->_fd)
      close(this->_fd);
  }
  operator bool() const { return this->_fd > 0; }
  // operator FILE *() { return this->_file; }

  std::string_view getFilePath() { return this->_filePath; }
  [[nodiscard]] bool saveToPath(const std::string_view path) {
    assert(this->_fd > 0);
    unsigned char *buf = (unsigned char *)::mmap(
        0, this->_length, PROT_READ | PROT_WRITE, MAP_PRIVATE, this->_fd, 0);
    if (!buf)
      return false;
    int fd = ::open(path.data(), O_RDWR | O_DIRECT | O_SYNC | O_CREAT, S_IRWXU);
    if (fd < 0) {
      ::munmap(buf, this->_length);
      return false;
    }
    std::size_t bytesWritten = 0;
    while (bytesWritten < this->_length) {
      const ssize_t wrote =
          ::write(fd, buf + bytesWritten, this->_length - bytesWritten);
      if (wrote < 0) {
        ::close(fd);
        ::munmap(buf, this->_length);
        return false;
      }
      bytesWritten += wrote;
    }

    ::syncfs(fd);
    ::close(fd);
    ::munmap(buf, this->_length);
    return true;
  }
  [[nodiscard]] bool write(const std::span<const unsigned char> data) {
    assert(this->_fd > 0);
    std::size_t bytesWritten = 0;
    while (bytesWritten < data.size()) {
      const ssize_t wrote =
          ::write(this->_fd, &data[bytesWritten], data.size() - bytesWritten);
      if (wrote < 0)
        return false;

      bytesWritten += wrote;
      this->_length += wrote;
    }
    ::syncfs(this->_fd);
    return true;
  }

private:
  int _fd = -1;
  std::size_t _length = 0;
  std::string _filePath = "";
};
