#pragma once
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>
#include <vector>

class TempFile {
public:
  TempFile() {
    char TMP_TEMPLATE[] = "/var/tmp/FS_XXXXXX";
    this->_fd = mkstemp(TMP_TEMPLATE);
    this->_filePath = TMP_TEMPLATE;
  }
  TempFile(const TempFile &other) = delete;
  TempFile(TempFile &other) = delete;
  TempFile &operator=(const TempFile &other) = delete;
  TempFile &operator=(TempFile &&other) noexcept {
    std::swap(this->_fd, other._fd);
    return *this;
  }
  TempFile(TempFile &&other) noexcept : _fd(other._fd) { other._fd = -1; }
  ~TempFile() noexcept {
    if (this->_fd)
      close(this->_fd);
    unlink(this->_filePath.c_str());
  }
  std::optional<std::vector<unsigned char>> getContents() const {

    std::vector<unsigned char> data;
    if (std::ifstream file(this->_filePath, std::fstream::binary); !file) {
      std::cerr << "Unable to open file\n";
      return std::nullopt;
    } else {
      file.seekg(0, file.end);
      const int length = file.tellg();
      data.resize(length);
      file.seekg(0, file.beg);

      file.read(reinterpret_cast<char *>(data.data()), length);
    }
    return data;
  }
  operator bool() const { return this->_fd > 0; }
  // operator FILE *() { return this->_file; }

  std::string_view getFilePath() const { return this->_filePath; }
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
        if (errno == EINTR)
          continue;
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
      if (wrote < 0) {
        if (errno == EINTR)
          continue;
        return false;
      }

      bytesWritten += wrote;
      this->_length += wrote;
      this->_sync += wrote;
    }
    if (this->_sync > (1024 * 1024 * 512))
      this->sync();
    return true;
  }

  void sync() {
    this->_sync = 0;
    ::syncfs(this->_fd);
  }

private:
  int _fd = -1;
  std::uint64_t _length = 0;
  uint32_t _sync = 0;
  std::string _filePath = "";
};
