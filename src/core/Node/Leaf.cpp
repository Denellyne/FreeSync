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

  DiskFile file = DiskFile(this->_filePath.string());

  this->_hash = this->hash(file.toSpan());
  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);

  this->_objPath = path;

  if (!this->writeBlob(compressData(file).toSpan()))
    throw std::runtime_error("Unable to write leaf blob\n");
}

Leaf::Leaf(const std::string_view filePath, const std::span<unsigned char> data,
           const bool isExecutable) {

  this->_filePath = filePath;
  this->_isExecutable = isExecutable;

  this->_hash = this->hash(data);
  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);

  this->_objPath = path;

  DiskFile file = compressData(data);

  if (!this->writeBlob(file.toSpan()))
    throw std::runtime_error("Unable to write leaf blob\n");
}

Leaf::Leaf(const std::string_view filePath, const std::span<unsigned char> data,
           const std::string_view parentHash, const bool isExecutable) {

  this->_filePath = filePath;
  this->_isExecutable = isExecutable;

  this->_hash = this->hash(data);
  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);

  this->_objPath = path;
  DiskFile file = compressData(data);

  if (!this->writeDiffBlob(file.toSpan(), parentHash))
    throw std::runtime_error("Unable to write leaf blob\n");
}
bool Leaf::writeFile(const std::string_view path,
                     const std::span<unsigned char> data) {

  fs::path p = path;
  fs::create_directories(p.parent_path());
  std::ofstream file(p, std::fstream::binary);
  if (!file)
    return false;

  file.write(reinterpret_cast<const char *>(data.data()), data.size());
  file.flush();
  return true;
}
bool Leaf::writeDiffBlob(const std::span<unsigned char> data,
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
bool Leaf::writeBlob(const std::span<unsigned char> data) {
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
  DiskFile file(this->_objPath.string());
  if (!file)
    return std::unexpected("Invalid file");
  std::span<unsigned char> data = file.toSpan();

  int length = data.size();
  if (length < 6)
    return std::unexpected("Invalid file contents");
  char type[5]{'\0'};
  memcpy(type, data.data(), 4);
  if (!strcmp(type, "blob")) {
    std::span<unsigned char> res = Leaf::readBlobData(data.subspan(5));
    DiskFile decompressedRes = decompressData(res);
    if (!decompressedRes)
      return std::unexpected("Unable to decompress blob");
    return std::vector<unsigned char>{decompressedRes.toSpan().cbegin(),
                                      decompressedRes.toSpan().cend()};
  }
  if (length - 5 < 64)
    return std::unexpected(
        "Invalid file contents, couldn't read leaf parent hash");

  Leaf parent = this->getParentLeaf(data.subspan(5));
  if (auto dataOpt = parent.getFinalDecompressBlob(); !dataOpt.has_value())
    return std::unexpected(dataOpt.error());
  else {
    std::span<unsigned char> res = Leaf::readBlobData(data.subspan(5 + 64));
    DiskFile decompressedRes = decompressData(res);
    if (!decompressedRes)
      return std::unexpected("Unable to decompress diff blob");
    std::vector<unsigned char> final = std::move(dataOpt.value());
    if (!applyDiffsUncompressed(final, decompressedRes.toSpan()))
      return std::unexpected("Unable to apply diffs");
    return final;
  }
}

bool applyDiffsUncompressed(std::vector<unsigned char> &old,
                            const std::span<unsigned char> diffs) {

  if (diffs.empty())
    return true;
  const std::vector<unsigned char> original = std::move(old);
  old.clear();
  for (size_t idx = 0; idx < diffs.size();) {
    if (diffs[idx] == 'C') {
      idx++;
      std::array<unsigned char, 32> beg, end;
      strncpy((char *)beg.data(), (char *)&diffs[idx], 32);
      idx += 32;
      strncpy((char *)end.data(), (char *)&diffs[idx], 32);
      idx += 32;
      if (const auto begIdxOpt = toNumber(beg); !begIdxOpt.has_value())
        return false;
      else if (const auto endIdxOpt = toNumber(end); !endIdxOpt.has_value())
        return false;
      else {
        const uint32_t begIdx = begIdxOpt.value();
        const uint32_t endIdx = endIdxOpt.value();
        old.insert(old.end(), original.begin() + begIdx,
                   original.begin() + endIdx);
      }
    } else if (diffs[idx] == 'I') {
      idx++;
      std::array<unsigned char, 32> size;
      strncpy((char *)size.data(), (char *)&diffs[idx], 32);
      idx += 32;
      if (const auto lengthOpt = toNumber(size); !lengthOpt.has_value())
        return false;
      else {
        const uint32_t length = lengthOpt.value();
        old.insert(old.end(), diffs.begin() + idx,
                   diffs.begin() + idx + length);
        idx += length;
      }
    } else
      return false;
  }
  return true;
}

std::expected<std::vector<unsigned char>, std::string>
Leaf::diffFile(const std::span<unsigned char> newer) {
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

std::span<unsigned char> Leaf::readBlobData(std::span<unsigned char> data) {

  std::string size = "";
  int pos = 0;
  for (const char c : data) {
    if (c == '\0')
      break;
    size += c;
    pos++;
  }
  assert(!size.empty());
  const uint32_t length = std::stoul(size);

  return data.subspan(pos + 1, length);
}

Leaf Leaf::getParentLeaf(const std::span<unsigned char> data) {
  const std::string_view parentHash = std::string_view((char *)data.data(), 64);
  return Leaf(this->getFilePath().string(), parentHash, this->_isExecutable);
}
