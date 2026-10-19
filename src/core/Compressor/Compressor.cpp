#include "Compressor.h"
#include "../Networking/FSProtocol.h"
#include <print>

// bool compressData(std::string &data) {
//   if (data.empty())
//     return true;
//
//   std::string tmp = "";
//   tmp.resize(compressBound(data.size()));
//
//   uLongf size = tmp.size();
//
//   int res = compress2(reinterpret_cast<unsigned char *>(&tmp[0]), &size,
//                       reinterpret_cast<unsigned char *>(&data[0]),
//                       data.size(), COMPRESSION_LEVEL);
//
//   if (res != Z_OK)
//     return false;
//
//   tmp.resize(size);
//   data = std::move(tmp);
//   return true;
// }
bool Compressor::compressData(std::vector<unsigned char> &vec) noexcept {
  alignas(32) unsigned char out[FSMANAGER_PACKET];

  vec.clear();
  vec.reserve(FSMANAGER_PACKET);

  if (_strm.avail_in == 0 && !this->_data.empty()) {
    const std::size_t chunkSize = std::min<std::size_t>(
        this->_data.size(), std::numeric_limits<uInt>::max());
    _strm.next_in = const_cast<unsigned char *>(this->_data.data());
    _strm.avail_in = static_cast<uInt>(chunkSize);
    this->_data = this->_data.subspan(chunkSize);
  }

  _strm.next_out = out;
  _strm.avail_out = FSMANAGER_PACKET;

  const int flush = this->_data.empty() ? Z_FINISH : Z_NO_FLUSH;
  const int ret = zng_deflate(&_strm, flush);

  if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR)
    return false;

  const std::size_t have = FSMANAGER_PACKET - _strm.avail_out;
  vec.insert(vec.end(), out, out + have);

  if (ret == Z_STREAM_END)
    this->_finished = true;

  return true;
}
bool compressData(std::vector<unsigned char> &data) {
  if (data.empty())
    return true;

  std::vector<unsigned char> tmp(zng_compressBound(data.size()));

  uLongf size = tmp.size();

  int res = zng_compress2(tmp.data(), &size, data.data(), data.size(),
                          COMPRESSION_LEVEL);

  if (res != Z_OK)
    return false;

  tmp.resize(size);
  data = std::move(tmp);
  return true;
}
bool Decompressor::decompressData() noexcept {
  DiskFile f;

  if (this->_data.empty())
    return true;

  zng_stream zs{};
  if (zng_inflateInit2(&zs, 15 + 32) != Z_OK)
    return false;

  constexpr size_t CHUNK = 64 * 1024;
  std::span<const unsigned char> remaining = this->_data;
  int ret = Z_OK;

  do {
    if (zs.avail_in == 0 && !remaining.empty()) {
      const size_t chunkSize =
          std::min<size_t>(remaining.size(), std::numeric_limits<uInt>::max());
      zs.next_in = const_cast<unsigned char *>(remaining.data());
      zs.avail_in = static_cast<uInt>(chunkSize);
      remaining = remaining.subspan(chunkSize);
    }

    alignas(32) unsigned char buf[CHUNK];
    zs.next_out = buf;
    zs.avail_out = CHUNK;

    ret = zng_inflate(&zs, Z_NO_FLUSH);

    if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) [[unlikely]] {
      zng_inflateEnd(&zs);
      return false;
    }

    const size_t produced = CHUNK - zs.avail_out;
    if (produced > 0) {
      if (!f.write(std::span<const unsigned char>(buf, produced))) {
        zng_inflateEnd(&zs);
        return false;
      }
    } else if (ret == Z_BUF_ERROR) [[unlikely]] {
      zng_inflateEnd(&zs);
      return false;
    }

  } while (ret != Z_STREAM_END);

  zng_inflateEnd(&zs);

  this->_file = std::move(f);
  this->_data = this->_file.toSpan();
  return true;
}

// bool decompressData(std::vector<unsigned char> &data) {
//   if (data.empty())
//     return true;
//
//   z_stream zs{};
//   zs.next_in = data.data();
//   zs.avail_in = data.size();
//
//   if (inflateInit2(&zs, 15 + 32) != Z_OK)
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
//     zs.next_out = out.data() + start;
//     zs.avail_out = CHUNK;
//
//     ret = inflate(&zs, Z_NO_FLUSH);
//
//     if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
//         [[unlikely]] {
//       inflateEnd(&zs);
//       return false;
//     }
//
//     out.resize(start + (CHUNK - zs.avail_out));
//
//   } while (ret != Z_STREAM_END);
//
//   inflateEnd(&zs);
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

DiskFile compressData(DiskFile &file) noexcept {
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

  zng_stream zs{};
  if (zng_inflateInit2(&zs, 15 + 32) != Z_OK) [[unlikely]] {
    std::println("Unable to initialize decompression");
    return DiskFile(true);
  }

  constexpr size_t OUT_CHUNK = 64 * 1024;
  constexpr size_t MAX_IN_CHUNK = 1024 * 1024 * 1024;

  size_t bytesRemaining = data.size();
  const unsigned char *currentIn = data.data();

  int ret = Z_OK;

  while (bytesRemaining > 0 || ret != Z_STREAM_END) {
    if (zs.avail_in == 0 && bytesRemaining > 0) {
      const size_t chunkIn = std::min(bytesRemaining, MAX_IN_CHUNK);
      zs.next_in = const_cast<unsigned char *>(currentIn);
      zs.avail_in = static_cast<unsigned int>(chunkIn);

      currentIn += chunkIn;
      bytesRemaining -= chunkIn;
    }

    unsigned char buf[OUT_CHUNK];
    zs.next_out = buf;
    zs.avail_out = static_cast<unsigned int>(OUT_CHUNK);

    ret = zng_inflate(&zs, Z_NO_FLUSH);
    const size_t produced = OUT_CHUNK - zs.avail_out;

    if (produced > 0 &&
        !out.write(std::span<const unsigned char>(buf, produced)))
        [[unlikely]] {
      std::println("Error writing to file");
      zng_inflateEnd(&zs);
      return DiskFile(true);
    }

    if (ret == Z_STREAM_END)
      break;

    if (ret != Z_OK) [[unlikely]] {
      std::println("Error decompressing: {}", ret);
      zng_inflateEnd(&zs);
      return DiskFile(true);
    }
  }

  zng_inflateEnd(&zs);
  return out;
}
