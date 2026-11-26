#include "Compressor.h"
#include "../Networking/FSProtocol.h"
#include "../defines.hpp"
#include <cstddef>
#include <print>

bool Compressor::compressData(std::vector<unsigned char> &vec) noexcept {
  alignas(ALIGNMENT) std::array<unsigned char, FSMANAGER_PACKET> out;

  vec.clear();
  vec.reserve(FSMANAGER_PACKET);

  if (_strm.avail_in == 0 && !this->_data.empty()) {
    const std::size_t chunkSize = std::min<std::size_t>(
        this->_data.size(), std::numeric_limits<uInt>::max());
    _strm.next_in = const_cast<unsigned char *>(this->_data.data());
    _strm.avail_in = static_cast<uInt>(chunkSize);
    this->_data = this->_data.subspan(chunkSize);
  }

  _strm.next_out = out.data();
  _strm.avail_out = FSMANAGER_PACKET;

  const int flush = this->_data.empty() ? Z_FINISH : Z_NO_FLUSH;
  const int ret = zng_deflate(&_strm, flush);

  if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
    _strm.next_out = nullptr;
    return false;
  }

  const std::size_t have = FSMANAGER_PACKET - _strm.avail_out;
  vec.insert(vec.end(), out.data(), out.data() + have);

  if (ret == Z_STREAM_END)
    this->_finished = true;

  _strm.next_out = nullptr;
  return true;
}
bool compressData(std::vector<unsigned char> &data) {
  if (data.empty())
    return true;

  std::vector<unsigned char> tmp(zng_compressBound(data.size()));

  size_t size = tmp.size();

  int res = zng_compress2(tmp.data(), &size, data.data(), data.size(),
                          COMPRESSION_LEVEL);

  if (res != Z_OK)
    return false;

  tmp.resize(size);
  data = std::move(tmp);
  return true;
}
bool Decompressor::decompressData() noexcept {
  DiskFile file;

  if (this->_data.empty())
    return true;

  zng_stream zngs{};
  if (zng_inflateInit2(&zngs, 15 + 32) != Z_OK)
    return false;

  constexpr size_t CHUNK = static_cast<const size_t>(64 * 1024);
  std::span<const unsigned char> remaining = this->_data;
  int ret = Z_OK;

  do {
    if (zngs.avail_in == 0 && !remaining.empty()) {
      const size_t chunkSize =
          std::min<size_t>(remaining.size(), std::numeric_limits<uInt>::max());
      zngs.next_in = const_cast<unsigned char *>(remaining.data());
      zngs.avail_in = static_cast<uInt>(chunkSize);
      remaining = remaining.subspan(chunkSize);
    }

    alignas(ALIGNMENT) std::array<unsigned char, CHUNK> buf;
    zngs.next_out = buf.data();
    zngs.avail_out = CHUNK;

    ret = zng_inflate(&zngs, Z_NO_FLUSH);

    if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) [[unlikely]] {
      zng_inflateEnd(&zngs);
      return false;
    }

    const size_t produced = CHUNK - zngs.avail_out;
    if (produced > 0) {
      if (!file.write(std::span<const unsigned char>(buf.begin(), produced))) {
        zng_inflateEnd(&zngs);
        return false;
      }
    } else if (ret == Z_BUF_ERROR) [[unlikely]] {
      zng_inflateEnd(&zngs);
      return false;
    }

  } while (ret != Z_STREAM_END);

  zng_inflateEnd(&zngs);

  this->_file = std::move(file);
  this->_data = this->_file.toSpan();
  return true;
}

// bool decompressData(std::vector<unsigned char> &data) {
//   if (data.empty())
//     return true;
//
//   z_stream zngs{};
//   zngs.next_in = data.data();
//   zngs.avail_in = data.size();
//
//   if (inflateInit2(&zngs, 15 + 32) != Z_OK)
//     return false;
//
//   std::vector<unsigned char> out;
//   constexpr size_t CHUNK = 64 * 1024;
//
//   int ret;
//   do {
//     size_t start = out.size();
//     out.resize(start + CHUNK);
//
//     zngs.next_out = out.data() + start;
//     zngs.avail_out = CHUNK;
//
//     ret = inflate(&zngs, Z_NO_FLUSH);
//
//     if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
//         [[unlikely]] {
//       inflateEnd(&zngs);
//       return false;
//     }
//
//     out.resize(start + (CHUNK - zngs.avail_out));
//
//   } while (ret != Z_STREAM_END);
//
//   inflateEnd(&zngs);
//
//   data.swap(out);
//   return true;
// }

DiskFile compressData(std::string_view path) noexcept {
  DiskFile file(path);
  Compressor comp(file.toSpan());
  if (!comp) {
    std::println("Unable to initialize compressor");
    return DiskFile(true);
  }
  DiskFile out;
  while (!comp._finished) {
    std::vector<unsigned char> vec;
    if (!comp.compressData(vec) || !out.write(vec)) {
      std::println("Unable to either write to file or compress chunk");
      return DiskFile(true);
    }
  }

  return out;
}

DiskFile compressData(const DiskFile &file) noexcept {
  Compressor comp(file.toSpan());
  if (!comp) {
    std::println("Unable to initialize compressor");
    return DiskFile(true);
  }
  DiskFile out;
  while (!comp._finished) {
    std::vector<unsigned char> vec;
    if (!comp.compressData(vec) || !out.write(vec)) {
      std::println("Unable to either write to file or compress chunk");
      return DiskFile(true);
    }
  }

  return out;
}
DiskFile compressData(std::span<unsigned char> data) noexcept {
  Compressor comp(data);
  if (!comp) {
    std::println("Unable to initialize compressor");
    return DiskFile(true);
  }
  DiskFile out;
  while (!comp._finished) {
    std::vector<unsigned char> vec;
    if (!comp.compressData(vec) || !out.write(vec)) {
      std::println("Unable to either write to file or compress chunk");
      return DiskFile(true);
    }
  }

  return out;
}

DiskFile decompressData(std::span<unsigned char> data) noexcept {
  DiskFile out;
  if (data.empty()) [[unlikely]] {
    std::println("Nothing to decompress, vector is empty");
    return DiskFile(true);
  }

  zng_stream zngs{};
  if (zng_inflateInit2(&zngs, 15 + 32) != Z_OK) [[unlikely]] {
    std::println("Unable to initialize decompression");
    return DiskFile(true);
  }

  constexpr size_t OUT_CHUNK = static_cast<const size_t>(64 * 1024);
  constexpr size_t MAX_IN_CHUNK =
      static_cast<const size_t>(1024L * 1024 * 1024);

  size_t bytesRemaining = data.size();
  const unsigned char *currentIn = data.data();

  int ret = Z_OK;

  while (bytesRemaining > 0 || ret != Z_STREAM_END) {
    if (zngs.avail_in == 0 && bytesRemaining > 0) {
      const size_t chunkIn = std::min(bytesRemaining, MAX_IN_CHUNK);
      zngs.next_in = const_cast<unsigned char *>(currentIn);
      zngs.avail_in = static_cast<unsigned int>(chunkIn);

      currentIn += chunkIn;
      bytesRemaining -= chunkIn;
    }

    std::array<unsigned char, OUT_CHUNK> buf;
    zngs.next_out = buf.data();
    zngs.avail_out = static_cast<unsigned int>(OUT_CHUNK);

    ret = zng_inflate(&zngs, Z_NO_FLUSH);
    const size_t produced = OUT_CHUNK - zngs.avail_out;

    if (produced > 0 &&
        !out.write(std::span<const unsigned char>(buf.begin(), produced)))
        [[unlikely]] {
      std::println("Error writing to file");
      zng_inflateEnd(&zngs);
      return DiskFile(true);
    }

    if (ret == Z_STREAM_END)
      break;

    if (ret != Z_OK) [[unlikely]] {
      std::println("Error decompressing: {}", ret);
      zng_inflateEnd(&zngs);
      return DiskFile(true);
    }
  }

  zng_inflateEnd(&zngs);
  return out;
}
