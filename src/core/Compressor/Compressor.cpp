#include "Compressor.h"
#include "../Networking/FSProtocol.h"
#include "../TempFile/TempFile.hpp"
#include <print>

bool compressData(std::string &data) {
  if (data.empty())
    return true;

  std::string tmp = "";
  tmp.resize(compressBound(data.size()));

  uLongf size = tmp.size();

  int res = compress2(reinterpret_cast<unsigned char *>(&tmp[0]), &size,
                      reinterpret_cast<unsigned char *>(&data[0]), data.size(),
                      Z_BEST_COMPRESSION);

  if (res != Z_OK)
    return false;

  tmp.resize(size);
  data = std::move(tmp);
  return true;
}
bool Compressor::compressData(std::vector<unsigned char> &vec) {
  unsigned char out[FSMANAGER_PACKET];

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
  const int ret = deflate(&_strm, flush);

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

  std::vector<unsigned char> tmp(compressBound(data.size()));

  uLongf size = tmp.size();

  int res = compress2(tmp.data(), &size, data.data(), data.size(),
                      Z_BEST_COMPRESSION);

  if (res != Z_OK)
    return false;

  tmp.resize(size);
  data = std::move(tmp);
  return true;
}
bool Decompressor::decompressData() {
  TempFile f;

  if (this->_data.empty())
    return true;

  z_stream zs{};
  if (inflateInit2(&zs, 15 + 32) != Z_OK)
    return false;

  constexpr size_t CHUNK = 16 * 1024;
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

    unsigned char buf[CHUNK];
    zs.next_out = buf;
    zs.avail_out = CHUNK;

    ret = inflate(&zs, Z_NO_FLUSH);

    if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
      inflateEnd(&zs);
      return false;
    }

    const size_t produced = CHUNK - zs.avail_out;
    if (produced > 0) {
      if (!f.write(std::span<const unsigned char>(buf, produced))) {
        inflateEnd(&zs);
        return false;
      }
    } else if (ret == Z_BUF_ERROR) {
      inflateEnd(&zs);
      return false;
    }

  } while (ret != Z_STREAM_END);

  inflateEnd(&zs);

  f.sync();
  this->_file = DiskFile(f.getFilePath());
  this->_data = this->_file.toSpan();
  return true;
}

bool decompressData(std::vector<unsigned char> &data) {
  if (data.empty())
    return true;

  z_stream zs{};
  zs.next_in = data.data();
  zs.avail_in = data.size();

  if (inflateInit2(&zs, 15 + 32) != Z_OK)
    return false;

  std::vector<unsigned char> out;
  constexpr size_t CHUNK = 16 * 1024;

  int ret;
  do {
    size_t start = out.size();
    out.resize(start + CHUNK);

    zs.next_out = out.data() + start;
    zs.avail_out = CHUNK;

    ret = inflate(&zs, Z_NO_FLUSH);

    if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
      inflateEnd(&zs);
      return false;
    }

    out.resize(start + (CHUNK - zs.avail_out));

  } while (ret != Z_STREAM_END);

  inflateEnd(&zs);

  data.swap(out);
  return true;
}

DiskFile compressData(std::string_view path) {
  DiskFile file(path);
  Compressor comp(file.toSpan());
  TempFile f;
  while (!comp._finished) {
    std::vector<unsigned char> vec;
    if (!comp.compressData(vec) || !f.write(vec))
      std::println("faah");
  }
  f.sync();

  return DiskFile(f.getFilePath());
}

DiskFile compressData(DiskFile &file) {
  Compressor comp(file.toSpan());
  TempFile f;
  while (!comp._finished) {
    std::vector<unsigned char> vec;
    if (!comp.compressData(vec) || !f.write(vec))
      std::println("faah");
  }

  f.sync();
  return DiskFile(f.getFilePath());
}
DiskFile compressData(std::span<unsigned char> data) {
  Compressor comp(data);
  TempFile f;
  while (!comp._finished) {
    std::vector<unsigned char> vec;
    if (!comp.compressData(vec) || !f.write(vec))
      std::println("faah");
  }

  f.sync();
  return DiskFile(f.getFilePath());
}

DiskFile decompressData(std::span<unsigned char> data) {
  TempFile f;
  if (data.empty()) {

    std::println("B");
    return DiskFile(f.getFilePath());
  }

  z_stream zs{};
  zs.next_in = data.data();
  zs.avail_in = data.size();

  if (inflateInit2(&zs, 15 + 32) != Z_OK) {
    std::println("C");
    return DiskFile(f.getFilePath());
  }

  constexpr size_t CHUNK = 16 * 1024;

  int ret;
  do {
    unsigned char out[CHUNK];

    zs.next_out = out;
    zs.avail_out = CHUNK;

    ret = inflate(&zs, Z_NO_FLUSH);
    const size_t produced = CHUNK - zs.avail_out;

    if (ret != Z_OK && ret != Z_STREAM_END) {
      std::println("A");
      inflateEnd(&zs);
      return DiskFile(f.getFilePath());
    }

    if (!f.write(std::span<const unsigned char>(out, produced))) {
      inflateEnd(&zs);
      return DiskFile(f.getFilePath());
    }

  } while (ret != Z_STREAM_END);

  inflateEnd(&zs);

  f.sync();
  return DiskFile(f.getFilePath());
}
