#pragma once
#include "../DiskObject/DiskFile.hpp"
#include <cassert>
#include <fcntl.h>
#include <span>
#include <stdlib.h>
#include <string_view>
#include <sys/mman.h>
#include <vector>
#include <zlib-ng.h>
#define COMPRESSION_LEVEL Z_BEST_COMPRESSION

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
