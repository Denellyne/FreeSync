#include "Leaf.h"
#include "../Compressor/Compressor.h"
#include "../DiskObject/DiskView.hpp"
#include "../Encrypt/Encrypt.h"
#include "Node.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <print>
#include <string>
#include <zlib-ng.h>

Leaf::Leaf(const std::string_view filePath) noexcept {

  if (!fs::exists(filePath)) [[unlikely]] {
    std::string err = "Passed file doesn't exist ";
    err += filePath;
    std::println("{}", err);

    this->_error = true;
    return;

  } else if (!fs::is_regular_file(filePath)) [[unlikely]] {
    std::string err = "Passed path isn't a file ";
    err += filePath;
    std::println("{}", err);

    this->_error = true;
    return;
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

  if (!this->writeBlob(compressData(file).toSpan())) [[unlikely]] {
    std::println("Unable to compress blob for leaf");
    this->_error = true;
    return;
  }
}

Leaf::Leaf(const std::string_view filePath, const std::span<unsigned char> data,
           const bool isExecutable, const std::string_view hash) noexcept {

  this->_filePath = filePath;
  this->_isExecutable = isExecutable;

  this->_hash = hash;
  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);

  this->_objPath = path;
  if (!this->writeBlob(data)) [[unlikely]] {
    std::println("Unable to write compressed blob to file");
    this->_error = true;
    return;
  }
}
Leaf::Leaf(const std::string_view filePath, const std::span<unsigned char> data,
           const bool isExecutable) noexcept {

  this->_filePath = filePath;
  this->_isExecutable = isExecutable;

  this->_hash = this->hash(data);
  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);

  this->_objPath = path;

  if (DiskFile file = compressData(data);
      !file || !this->writeBlob(file.toSpan())) [[unlikely]] {

    std::println("Unable to compress blob for leaf or write it to disk");
    this->_error = true;
    return;
  }
}

Leaf::Leaf(const std::string_view filePath, const std::span<unsigned char> data,
           const std::string_view parentHash,
           const bool isExecutable) noexcept {

  this->_filePath = filePath;
  this->_isExecutable = isExecutable;

  this->_hash = this->hash(data);
  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);

  this->_objPath = path;
  DiskFile file = compressData(data);

  if (!file || !this->writeDiffBlob(file.toSpan(), parentHash)) [[unlikely]] {
    std::println("Unable to compress blob for leaf or write diff blob");
    this->_error = true;
    return;
  }
}
bool Leaf::writeFile(const std::string_view path,
                     const std::span<unsigned char> data) noexcept {

  fs::path p = path;
  fs::create_directories(p.parent_path());

  DiskFile file;
  if (!file) [[unlikely]] {
    std::println("Unable to create temporary file");
    return false;
  }
  if (!file.write(data)) [[unlikely]] {
    std::println("Unable to move temporary tree blob");
    return false;
  }
  if (!file.moveFile(p)) [[unlikely]] {
    std::println("Unable to move temporary tree blob");
    return false;
  }
  return true;
}
bool Leaf::writeDiffBlob(const std::span<unsigned char> data,
                         const std::string_view parentHash) noexcept {
  fs::path path = this->_objPath;
  fs::create_directories(path.parent_path());

  DiskFile file;
  if (!file) [[unlikely]] {
    std::println("Unable to create temporary file");
    return false;
  }
  if (!file.write("diff ")) [[unlikely]] {
    std::println("Unable to move temporary tree blob");
    return false;
  }
  if (!file.write(parentHash)) [[unlikely]] {
    std::println("Unable to move temporary tree blob");
    return false;
  }
  if (!file.write(std::to_string(data.size()))) [[unlikely]] {
    std::println("Unable to move temporary tree blob");
    return false;
  }
  if (!file.write("\0")) [[unlikely]] {
    std::println("Unable to move temporary tree blob");
    return false;
  }
  if (!file.write(data)) [[unlikely]] {
    std::println("Unable to move temporary tree blob");
    return false;
  }
  if (!file.moveFile(path)) [[unlikely]] {
    std::println("Unable to move temporary tree blob");
    return false;
  }
  return true;
}
bool Leaf::writeBlob(const std::span<unsigned char> data) noexcept {
  fs::path path = this->_objPath;
  fs::create_directories(path.parent_path());

  DiskFile file;
  if (!file) [[unlikely]] {
    std::println("Unable to create temporary file");
    return false;
  }
  if (!file.write("blob ")) [[unlikely]] {
    std::println("Unable to move temporary tree blob");
    return false;
  }
  if (!file.write(std::to_string(data.size()))) [[unlikely]] {
    std::println("Unable to move temporary tree blob");
    return false;
  }
  if (!file.write("\0")) [[unlikely]] {
    std::println("Unable to move temporary tree blob");
    return false;
  }
  if (!file.write(data)) [[unlikely]] {
    std::println("Unable to move temporary tree blob");
    return false;
  }
  if (!file.moveFile(path)) [[unlikely]] {
    std::println("Unable to move temporary tree blob");
    return false;
  }

  return true;
}
std::expected<std::vector<unsigned char>, std::string>
Leaf::getBlob() noexcept {
  std::vector<unsigned char> res;
  DiskView file(this->_objPath.c_str());
  if (!file)
    return std::unexpected("Invalid file");
  auto contents = file.toSpan();

  size_t length = contents.size();
  if (length < 6)
    return std::unexpected("Invalid file contents");
  contents = contents.subspan(5);
  std::string size = "";
  char c = contents.front();
  contents = contents.subspan(1);
  while (c != '\0') {
    size += c;
    c = contents.front();
    contents = contents.subspan(1);
  }
  assert(!size.empty());
  length = std::stoul(size);

  res.resize(length);
  memcpy(res.data(), contents.data(), length);

  return res;
}

std::expected<DiskFile, std::string> Leaf::getFinalDecompressFile() noexcept {
  std::expected<DiskFile, std::string> result;
  DiskView file(this->_objPath.string());
  std::span<unsigned char> data = file.toSpan();
  if (!file)
    return std::unexpected("Invalid file");

  size_t length = data.size();
  if (length < 6)
    return std::unexpected("Invalid file contents");
  char type[5]{'\0'};
  memcpy(type, data.data(), 4);
  if (!strcmp(type, "blob")) {
    std::span<unsigned char> res = Leaf::readBlobData(data.subspan(5));
    DiskFile decompressedRes = decompressData(res);
    if (!decompressedRes)
      return std::unexpected("Unable to decompress blob");
    result.emplace(std::move(decompressedRes));
    return result;
  }
  if (length - 5 < 64)
    return std::unexpected(
        "Invalid file contents, couldn't read leaf parent hash");

  Leaf parent = this->getParentLeaf(data.subspan(5));
  if (auto dataOpt = parent.getFinalDecompressFile(); !dataOpt.has_value())
    return std::unexpected(dataOpt.error());
  else {
    std::span<unsigned char> res = Leaf::readBlobData(data.subspan(5 + 64));
    DiskFile decompressedRes = decompressData(res);
    if (!decompressedRes)
      return std::unexpected("Unable to decompress diff blob");
    DiskFile final = applyDiffsUncompressedFile(std::move(dataOpt.value()),
                                                std::move(decompressedRes));
    if (!final)
      return std::unexpected("Unable to apply diffs");
    result.emplace(std::move(final));
    return result;
  }
}
std::expected<std::vector<unsigned char>, std::string>
Leaf::getFinalDecompressBlob() noexcept {
  DiskView file(this->_objPath.string());
  if (!file)
    return std::unexpected("Invalid file");
  std::span<unsigned char> data = file.toSpan();

  size_t length = data.size();
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
      alignas(32) std::array<unsigned char, 32> beg, end;
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
      alignas(32) std::array<unsigned char, 32> size;
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
Leaf::diffFile(const std::span<unsigned char> newer,
               const bool isCompressed) noexcept {
  if (auto dataOpt = this->getFinalDecompressBlob(); !dataOpt.has_value())
    return std::unexpected(dataOpt.error());
  else {
    std::span<unsigned char> newBlob = newer;
    DiskFile file(true);
    if (isCompressed) {
      file = decompressData(newBlob);
      if (!file)
        return std::unexpected("Unable to decompress blob for diffing");

      newBlob = file.toSpan();
    }

    std::vector<unsigned char> diffs;
    const std::vector<unsigned char> original = std::move(dataOpt.value());
    if (Node::hash(newBlob) == Node::hash(original))
      return std::vector<unsigned char>{};

    auto originalIt = original.cbegin();
    auto newIt = newBlob.cbegin();
    while (newIt != newBlob.cend()) {
      if (originalIt == original.cend()) {
        if (const uint32_t length = newBlob.cend() - newIt; length > 0) {
          diffs.emplace_back('I');
          const auto lenArr = toArray(length);
          diffs.insert(diffs.end(), lenArr.cbegin(), lenArr.cend());
          diffs.insert(diffs.end(), newIt, newBlob.cend());
        }
        return diffs;
      }
      if (*newIt == *originalIt) {
        const auto beg = toArray(originalIt - original.cbegin());
        while (originalIt != original.cend() && newIt != newBlob.cend() &&
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
        while (newIt != newBlob.cend() && *newIt != *originalIt)
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

std::span<unsigned char>
Leaf::readBlobData(std::span<unsigned char> data) noexcept {

  std::string size = "";
  size_t pos = 0;
  for (const char c : data) {
    if (c == '\0')
      break;
    size += c;
    pos++;
  }
  assert(!size.empty());
  const size_t length = std::stoull(size);

  return data.subspan(pos + 1, length);
}

Leaf Leaf::getParentLeaf(const std::span<unsigned char> data) noexcept {
  const std::string_view parentHash = std::string_view((char *)data.data(), 64);
  return Leaf(this->getFilePath().string(), parentHash, this->_isExecutable);
}

DiskFile applyDiffsUncompressedFile(DiskFile &&old, DiskFile &&diffs) {

  const std::span<const unsigned char> diffsSpan = diffs.toSpan();
  const std::span<const unsigned char> originalSpan = old.toSpan();
  if (diffsSpan.empty())
    return old;

  DiskFile final;
  if (!final)
    return DiskFile(true);

  for (size_t idx = 0; idx < diffsSpan.size();) {
    if (diffsSpan[idx] == 'C') {
      idx++;
      alignas(32) std::array<unsigned char, 32> beg, end;
      strncpy((char *)beg.data(), (char *)&diffsSpan[idx], 32);
      idx += 32;
      strncpy((char *)end.data(), (char *)&diffsSpan[idx], 32);
      idx += 32;
      if (const auto begIdxOpt = toNumber(beg); !begIdxOpt.has_value())
        return DiskFile(true);
      else if (const auto endIdxOpt = toNumber(end); !endIdxOpt.has_value())
        return DiskFile(true);
      else {
        const uint32_t begIdx = begIdxOpt.value();
        const uint32_t endIdx = endIdxOpt.value();
        if (!final.write(originalSpan.subspan(begIdx, endIdx - begIdx)))
          return DiskFile(true);
      }
    } else if (diffsSpan[idx] == 'I') {
      idx++;
      alignas(32) std::array<unsigned char, 32> size;
      strncpy((char *)size.data(), (char *)&diffsSpan[idx], 32);
      idx += 32;
      if (const auto lengthOpt = toNumber(size); !lengthOpt.has_value())
        return DiskFile(true);
      else {
        const uint32_t length = lengthOpt.value();
        if (!final.write(diffsSpan.subspan(idx, length)))
          return DiskFile(true);

        idx += length;
      }
    } else
      return DiskFile(true);
  }
  return final;
}
