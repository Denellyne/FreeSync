#pragma once
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <utility>
#include <vector>
#include <zlib.h>

class DiskFile {
public:
  DiskFile() {}
  DiskFile(const DiskFile &) = delete;
  // DiskFile(const DiskFile &other) {
  //   char TMP_TEMPLATE[] = "/var/tmp/FS_XXXXXX";
  //   this->_fd = mkstemp(TMP_TEMPLATE);
  //   this->_path = TMP_TEMPLATE;
  //   if (!other.getFilePath().empty())
  //     std::filesystem::copy_file(
  //         other.getFilePath(), this->_path,
  //         std::filesystem::copy_options::overwrite_existing);
  //
  //   this->_length = std::filesystem::file_size(this->_path);
  //
  //   this->_buf = (unsigned char *)::mmap(
  //       0, this->_length, PROT_READ | PROT_WRITE, MAP_PRIVATE, this->_fd, 0);
  //   if (!this->_buf || this->_buf == MAP_FAILED) {
  //     this->_error = true;
  //     this->_buf = nullptr;
  //     return;
  //   }
  //   if (this->_fd < 0) {
  //     ::munmap(this->_buf, this->_length);
  //     this->_buf = nullptr;
  //     this->_error = true;
  //     return;
  //   }
  // }
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
  DiskFile(std::string_view filePath) {
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
  operator bool() const { return !this->_error; }
  ~DiskFile() {

    if (this->_buf)
      ::munmap(this->_buf, this->_length);
    if (this->_fd >= 0)
      ::close(this->_fd);

    if (!this->_path.empty())
      unlink(this->_path.c_str());
  }
  std::span<unsigned char> toSpan() const {
    return std::span<unsigned char>(this->_buf, this->_length);
  }

  bool error() { return this->_error; }
  std::string_view getFilePath() const { return this->_path; }

private:
  unsigned char *_buf = nullptr;
  std::string _path;
  uint64_t _length = 0;
  int _fd = -1;
  bool _error = false;
};
class Compressor {
public:
  Compressor(std::span<unsigned char> data) : _data(data) {

    _strm.zalloc = Z_NULL;
    _strm.zfree = Z_NULL;
    _strm.opaque = Z_NULL;

    int ret = deflateInit(&_strm, Z_BEST_COMPRESSION);
    if (ret != Z_OK) {
      throw std::runtime_error("Unable to initiate deflat");
      return;
    }
  }
  ~Compressor() { deflateEnd(&_strm); }

  bool compressData(std::vector<unsigned char> &vec);
  bool _finished = false;

private:
  std::span<unsigned char> _data;
  z_stream _strm{};
};
class Decompressor {
public:
  Decompressor(std::string_view filePath) : _file(DiskFile(filePath)) {
    this->_data = this->_file.toSpan();

    if (!this->decompressData())
      throw std::runtime_error("Unable to decompress data");
  }

  std::span<unsigned char> getData() { return this->_file.toSpan(); }

private:
  bool decompressData();
  std::span<unsigned char> _data;
  DiskFile _file;
};
DiskFile compressData(std::string_view path);
DiskFile compressData(DiskFile &file);
DiskFile compressData(std::span<unsigned char> data);
DiskFile decompressData(std::span<unsigned char> data);

// [[nodiscard]] bool compressData(std::vector<unsigned char> &data);
// [[nodiscard]] bool compressData(std::string &data);
// [[nodiscard]] bool decompressData(std::vector<unsigned char> &data);
