#include "Compressor.h"

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

bool decompressData(std::vector<unsigned char> &data) {
  if (data.empty())
    return true;

  z_stream zs{};
  zs.next_in = data.data();
  zs.avail_in = data.size();

  if (inflateInit(&zs) != Z_OK)
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
