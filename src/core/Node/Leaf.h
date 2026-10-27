#pragma once
#include "../DiskObject/DiskFile.hpp"
#include "Node.h"
#include <expected>
#include <vector>

[[nodiscard]] bool applyDiffsUncompressed(std::vector<unsigned char> &old,
                                          const std::span<unsigned char> diffs);

[[nodiscard]] DiskFile applyDiffsUncompressedFile(DiskFile &&file,
                                                  DiskFile &&diffs);
class Leaf : public Node {
public:
  explicit Leaf(const std::string_view filePath) noexcept;
  explicit Leaf(const std::string_view filePath, std::span<unsigned char> data,
                const bool isExecutable) noexcept;
  explicit Leaf(const std::string_view filePath, std::span<unsigned char> data,
                const bool isExecutable, const std::string_view hash) noexcept;
  explicit Leaf(const std::string_view filePath, std::span<unsigned char> data,
                const std::string_view parentHash,
                const bool isExecutable) noexcept;
  explicit Leaf(const std::string_view filePath, const std::string_view hash,
                const bool isExecutable) noexcept
      : _isExecutable(isExecutable) {
    this->_hash = hash;
    this->_filePath = filePath;
    std::string path = OBJFOLDER;
    path.append(this->_hash, 0, 2);
    path += '/';
    path.append(this->_hash, 2, 62);

    this->_objPath = path;
  }
  bool isExecutable() const noexcept { return this->_isExecutable; }

  constexpr const std::string_view getHash() const noexcept override {
    return this->_hash;
  }
  constexpr const fs::path &getFilePath() const noexcept override {
    return this->_filePath;
  }
  constexpr const fs::path &getObjPath() const noexcept override {
    return this->_objPath;
  }
  constexpr const std::string getFileName() const noexcept override {
    return this->_filePath.filename().string();
  }

  std::expected<std::vector<unsigned char>, std::string_view>
  diffFile(const std::span<unsigned char> newer,
           const bool isCompressed = false) noexcept;

  std::expected<std::vector<unsigned char>, std::string_view>
  getBlob() noexcept;
  std::expected<DiskFile, std::string_view> getFinalDecompressFile() noexcept;
  std::expected<std::vector<unsigned char>, std::string_view>
  getFinalDecompressBlob() noexcept;
  [[nodiscard]] static bool
  writeFile(const std::string_view path,
            const std::span<unsigned char> data) noexcept;

  static std::span<unsigned char>
  readBlobData(std::span<unsigned char> data) noexcept;
  Leaf getParentLeaf(const std::span<unsigned char> data) noexcept;

private:
  [[nodiscard]] bool writeBlob(const std::span<unsigned char> data) noexcept;
  [[nodiscard]] bool writeDiffBlob(const std::span<unsigned char> data,
                                   const std::string_view parentHash) noexcept;
  bool _isExecutable = false;
};
