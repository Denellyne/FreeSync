#pragma once
#include <string>
#include <vector>
#include <zlib.h>
[[nodiscard]] bool compressData(std::vector<unsigned char> &data);
[[nodiscard]] bool compressData(std::string &data);
[[nodiscard]] bool decompressData(std::vector<unsigned char> &data);
