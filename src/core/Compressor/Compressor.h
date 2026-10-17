#pragma once
#include <cassert>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <span>
#include <stdlib.h>
#include <string>
#include <sys/mman.h>
#include <utility>
#include <vector>
#include <zlib-ng.h>
#define COMPRESSION_LEVEL Z_DEFAULT_COMPRESSION

class DiskFile {
public:
  DiskFile(const bool error = false) noexcept : _error(error) {
    char TMP_TEMPLATE[] = "/var/tmp/FS_XXXXXX";
    this->_fd = mkstemp(TMP_TEMPLATE);
    this->_path = TMP_TEMPLATE;
    this->_length = 0;
    if (this->_fd < 0) {
      this->_error = true;
      this->_buf = nullptr;
      return;
    }
  }

  DiskFile(const DiskFile &) = delete;
  DiskFile(DiskFile &other) = delete;
  DiskFile &operator=(const DiskFile &other) = delete;
  DiskFile &operator=(DiskFile &&other) noexcept {
    std::swap(this->_fd, other._fd);
    std::swap(this->_length, other._length);
    std::swap(this->_buf, other._buf);
    std::swap(this->_path, other._path);
    return *this;
  }
  DiskFile(DiskFile &&other) noexcept {
    std::swap(this->_fd, other._fd);
    std::swap(this->_length, other._length);
    std::swap(this->_buf, other._buf);
    std::swap(this->_path, other._path);
  }
  DiskFile(std::string_view filePath) noexcept {
    char TMP_TEMPLATE[] = "/var/tmp/FS_XXXXXX";
    this->_fd = mkstemp(TMP_TEMPLATE);
    this->_path = TMP_TEMPLATE;
    std::filesystem::copy_file(
        filePath, this->_path,
        std::filesystem::copy_options::overwrite_existing);

    this->_length = std::filesystem::file_size(filePath);

    this->_buf = (unsigned char *)::mmap(
        0, this->_length, PROT_READ | PROT_WRITE, MAP_PRIVATE, this->_fd, 0);
    if (!this->_buf || this->_buf == MAP_FAILED) {
      this->_error = true;
      this->_buf = nullptr;
      return;
    }
    if (this->_fd < 0) {
      ::munmap(this->_buf, this->_length);
      this->_error = true;
      this->_buf = nullptr;
      return;
    }
  }
  operator bool() const noexcept { return !this->_error; }
  ~DiskFile() noexcept {

    if (this->_buf)
      ::munmap(this->_buf, this->_length);
    if (this->_fd >= 0) {
      this->sync();
      ::close(this->_fd);
    }
    if (!this->_path.empty())
      unlink(this->_path.c_str());
  }
  std::span<unsigned char> toSpan() const noexcept {
    if (!this->_buf) {
      const_cast<DiskFile *>(this)->_buf = (unsigned char *)::mmap(
          0, this->_length, PROT_READ | PROT_WRITE, MAP_PRIVATE, this->_fd, 0);
      if (!this->_buf || this->_buf == MAP_FAILED) {
        const_cast<DiskFile *>(this)->_error = true;
        const_cast<DiskFile *>(this)->_buf = nullptr;
        return {};
      }
    }
    return std::span<unsigned char>(this->_buf, this->_length);
  }

  bool error() noexcept { return this->_error; }
  std::string_view getFilePath() const noexcept { return this->_path; }

  [[nodiscard]] bool write(const std::span<const unsigned char> data) noexcept {
    assert(this->_fd >= 0);
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

private:
  void sync() noexcept {
    this->_sync = 0;
    ::syncfs(this->_fd);
  }
  unsigned char *_buf = nullptr;
  std::string _path = "";
  uint32_t _sync = 0;
  int _fd = -1;
  uint64_t _length = 0;
  bool _error = false;
};
class Compressor {
public:
  Compressor(std::span<unsigned char> data) noexcept : _data(data) {

    _strm.zalloc = Z_NULL;
    _strm.zfree = Z_NULL;
    _strm.opaque = Z_NULL;

    int ret = zng_deflateInit2(&_strm, COMPRESSION_LEVEL, Z_DEFLATED, 15 + 16,
                               9, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
      this->_error = true;
      return;
    }
  }
  ~Compressor() noexcept {
    if (!this->_error)
      zng_deflateEnd(&_strm);
  }

  operator bool() const noexcept { return !this->_error; }
  bool compressData(std::vector<unsigned char> &vec) noexcept;
  bool _finished = false;
  bool _error = false;

private:
  std::span<unsigned char> _data;
  zng_stream _strm{};
};
class Decompressor {
public:
  Decompressor(const std::string_view filePath) noexcept
      : _file(DiskFile(filePath)) {
    this->_data = this->_file.toSpan();

    if (!this->decompressData())
      this->_error = true;
  }

  operator bool() const noexcept { return !this->_error; }
  std::span<unsigned char> getData() const noexcept {
    return this->_file.toSpan();
  }

private:
  bool decompressData() noexcept;
  std::span<unsigned char> _data;
  DiskFile _file;
  bool _error = false;
};
DiskFile compressData(std::string_view path) noexcept;
DiskFile compressData(DiskFile &file) noexcept;
DiskFile compressData(std::span<unsigned char> data) noexcept;
DiskFile decompressData(std::span<unsigned char> data) noexcept;

// [[nodiscard]] bool compressData(std::vector<unsigned char> &data);
// [[nodiscard]] bool compressData(std::string &data);
// [[nodiscard]] bool decompressData(std::vector<unsigned char> &data);
