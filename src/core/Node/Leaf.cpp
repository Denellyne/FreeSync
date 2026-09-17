#include "Leaf.h"
#include "../Compressor/Compressor.h"
#include "../Encrypt/Encrypt.h"
#include "Node.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <zlib.h>

Leaf::Leaf(const std::string_view filePath) {

  if (!fs::exists(filePath)) {
    std::string err = "Passed file doesn't exist ";
    err += filePath;
    err += '\n';
    throw std::runtime_error(err);
  } else if (!fs::is_regular_file(filePath)) {
    std::string err = "Passed path isn't a file ";
    err += filePath;
    err += '\n';
    throw std::runtime_error(err);
  }
  this->_filePath = filePath;
  if (const fs::perms permissions = fs::status(this->_filePath).permissions();
      (fs::perms::none != (permissions & fs::perms::owner_exec)) ||
      (fs::perms::none != (permissions & fs::perms::group_exec)) ||
      (fs::perms::none != (permissions & fs::perms::others_exec)))
    this->_isExecutable = true;

  std::vector<unsigned char> data;
  std::ifstream file(this->_filePath, std::fstream::binary);
  if (!file) {
    throw std::runtime_error("Unable to create leaf node\n");
    return;
  }

  file.seekg(0, file.end);
  const int length = file.tellg();
  data.resize(length);
  file.seekg(0, file.beg);

  file.read(reinterpret_cast<char *>(data.data()), length);

  this->_hash = this->hash(data);
  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);

  this->_objPath = path;
  if (!compressData(data))
    throw std::runtime_error("Unable to create leaf node\n");

  if (!this->writeBlob(data))
    throw std::runtime_error("Unable to write leaf blob\n");

  file.close();
}

Leaf::Leaf(const std::string_view filePath, std::vector<unsigned char> &data,
           const bool isExecutable) {

  this->_filePath = filePath;
  this->_isExecutable = isExecutable;

  this->_hash = this->hash(data);
  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);

  this->_objPath = path;
  if (!compressData(data))
    throw std::runtime_error("Unable to create leaf node\n");

  if (!this->writeBlob(data))
    throw std::runtime_error("Unable to write leaf blob\n");
}

Leaf::Leaf(const std::string_view filePath, std::vector<unsigned char> &data,
           const std::string_view parentHash, const bool isExecutable) {

  this->_filePath = filePath;
  this->_isExecutable = isExecutable;

  this->_hash = this->hash(data);
  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);

  this->_objPath = path;
  if (!compressData(data))
    throw std::runtime_error("Unable to create leaf node\n");

  if (!this->writeDiffBlob(data, parentHash))
    throw std::runtime_error("Unable to write leaf blob\n");
}
bool Leaf::writeFile(const std::string_view path,
                     const std::vector<unsigned char> &data) {

  fs::path p = path;
  fs::create_directories(p.parent_path());
  std::ofstream file(p, std::fstream::binary);
  if (!file)
    return false;

  file.write(reinterpret_cast<const char *>(data.data()), data.size());
  file.flush();
  return true;
}
bool Leaf::writeDiffBlob(const std::vector<unsigned char> &data,
                         const std::string_view parentHash) {
  fs::path path = this->_objPath;
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::fstream::binary);
  if (!file)
    return false;

  file.write("diff ", 5);
  file.write(parentHash.data(), 64);
  const std::string stringSize = std::to_string(data.size());
  file.write(stringSize.c_str(), stringSize.length());
  file.put('\0');
  file.write(reinterpret_cast<const char *>(data.data()), data.size());
  file.flush();
  return true;
}
bool Leaf::writeBlob(const std::vector<unsigned char> &data) {
  fs::path path = this->_objPath;
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::fstream::binary);
  if (!file)
    return false;

  file.write("blob ", 5);
  const std::string stringSize = std::to_string(data.size());
  file.write(stringSize.c_str(), stringSize.length());
  file.put('\0');
  file.write(reinterpret_cast<const char *>(data.data()), data.size());
  file.flush();
  return true;
}
std::expected<std::vector<unsigned char>, std::string> Leaf::getBlob() {
  std::vector<unsigned char> res;
  std::ifstream file(this->_objPath, std::fstream::binary);
  if (!file)
    return std::unexpected("Invalid file");

  file.seekg(0, file.end);
  int length = file.tellg();
  if (length < 6)
    return std::unexpected("Invalid file contents");
  file.seekg(5, file.beg);
  std::string size = "";
  char c = file.get();
  while (c != '\0') {
    size += c;
    c = file.get();
  }
  assert(!size.empty());
  length = std::stoul(size);

  res.resize(length);

  file.read(reinterpret_cast<char *>(res.data()), length);
  return res;
}

std::expected<std::vector<unsigned char>, std::string>
Leaf::getFinalDecompressBlob() {
  std::ifstream file(this->_objPath, std::fstream::binary);
  if (!file)
    return std::unexpected("Invalid file");

  file.seekg(0, file.end);
  int length = file.tellg();
  if (length < 6)
    return std::unexpected("Invalid file contents");
  char type[4]{'\0'};
  file.seekg(0, file.beg);
  file.read(type, 4);
  file.seekg(5, file.beg);
  if (!strcmp(type, "blob")) {
    std::vector<unsigned char> res = Leaf::readBlobData(file);
    if (!decompressData(res))
      return std::unexpected("Unable to decompress blob");
    return res;
  }
  if (length - 5 < 64)
    return std::unexpected(
        "Invalid file contents, couldn't read leaf parent hash");

  Leaf parent = this->getParentLeaf(file);
  if (auto dataOpt = parent.getFinalDecompressBlob(); !dataOpt.has_value())
    return std::unexpected(dataOpt.error());

  else {
    std::vector<unsigned char> res = Leaf::readBlobData(file);
    if (!decompressData(res))
      return std::unexpected("Unable to decompress diff blob");
    std::vector<unsigned char> final = std::move(dataOpt.value());
    if (!applyDiffsUncompressed(final, res))
      return std::unexpected("Unable to apply diffs");
    return final;
  }
}

bool applyDiffsUncompressed(std::vector<unsigned char> &old,
                            const std::vector<unsigned char> &diffs) {

  if (diffs.empty())
    return true;
  const std::vector<unsigned char> original = std::move(old);
  old.clear();
  for (int idx = 0; idx < diffs.size();) {
    if (diffs[idx] == 'C') {
      idx++;
      std::array<unsigned char, 32> beg, end;
      strncpy((char *)beg.data(), (char *)&diffs[idx], 32);
      idx += 32;
      strncpy((char *)end.data(), (char *)&diffs[idx], 32);
      idx += 32;
      const uint32_t begIdx = toNumber(beg);
      const uint32_t endIdx = toNumber(end);
      old.insert(old.end(), original.begin() + begIdx,
                 original.begin() + endIdx);
    } else if (diffs[idx] == 'I') {
      idx++;
      std::array<unsigned char, 32> size;
      strncpy((char *)size.data(), (char *)&diffs[idx], 32);
      idx += 32;
      const uint32_t length = toNumber(size);
      old.insert(old.end(), diffs.begin() + idx, diffs.begin() + idx + length);
      idx += length;
    } else
      return false;
  }
  return true;
}

std::expected<std::vector<unsigned char>, std::string>
Leaf::diffFile(const std::vector<unsigned char> &newer) {
  if (auto dataOpt = this->getFinalDecompressBlob(); !dataOpt.has_value())
    return std::unexpected(dataOpt.error());
  else {
    std::vector<unsigned char> diffs;
    const std::vector<unsigned char> original = std::move(dataOpt.value());
    if (Node::hash(newer) == Node::hash(original))
      return std::vector<unsigned char>{};

    auto originalIt = original.cbegin();
    auto newIt = newer.cbegin();
    while (newIt != newer.cend()) {
      if (originalIt == original.cend()) {
        if (const uint32_t length = newer.cend() - newIt; length > 0) {
          diffs.emplace_back('I');
          const auto lenArr = toArray(length);
          diffs.insert(diffs.end(), lenArr.cbegin(), lenArr.cend());
          diffs.insert(diffs.end(), newIt, newer.cend());
        }
        return diffs;
      }
      if (*newIt == *originalIt) {
        const auto beg = toArray(originalIt - original.cbegin());
        while (originalIt != original.cend() && newIt != newer.cend() &&
               *newIt == *originalIt) {
          newIt++;
          originalIt++;
        }
        const auto end = toArray(originalIt - original.cbegin());
        diffs.emplace_back('C');
        diffs.insert(diffs.end(), beg.cbegin(), beg.cend());
        diffs.insert(diffs.end(), end.cbegin(), end.cend());
      } else {
        const auto beg = newIt;
        while (newIt != newer.cend() && *newIt != *originalIt)
          newIt++;

        diffs.emplace_back('I');
        const auto lenArr = toArray(newIt - beg);
        diffs.insert(diffs.end(), lenArr.cbegin(), lenArr.cend());
        diffs.insert(diffs.end(), beg, newIt);
      }
    }
    return diffs;
  }
}

std::vector<unsigned char> Leaf::readBlobData(std::ifstream &file) {

  std::vector<unsigned char> res;
  std::string size = "";
  char c = file.get();
  while (c != '\0') {
    size += c;
    c = file.get();
  }
  assert(!size.empty());
  uint32_t length = std::stoul(size);

  res.resize(length);

  file.read(reinterpret_cast<char *>(res.data()), length);
  return res;
}

Leaf Leaf::getParentLeaf(std::ifstream &file) {
  std::string parentHash;
  parentHash.resize(64);
  file.read(parentHash.data(), 64);
  return Leaf(this->getFilePath().string(), parentHash, this->_isExecutable);
}
