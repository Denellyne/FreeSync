#pragma once
#include "Node.h"
#include <expected>
class LTree : public Node {
private:
  struct LNode {
    auto operator<=>(const LNode &) const noexcept = default;
    std::string _entry;
    std::string _fileName;
    std::string _hash;
    unsigned getFileSize() const noexcept;
  };

public:
  struct Commit {
    const std::string _objPath;
    const std::filesystem::file_time_type _timestamp;
    Commit() = delete;
    Commit(const std::string_view path,
           const std::filesystem::file_time_type tm) noexcept
        : _objPath(path), _timestamp(tm) {}
  };

public:
  LTree() noexcept = default;
  LTree(const std::array<char, 64> &hash, const std::string_view filePath,
        const bool root = false) noexcept;
  [[nodiscard]] bool writeMerkleTree() noexcept;
  static LTree newEmptyLTree(const std::string_view filePath) noexcept {
    LTree tree = LTree();
    tree._filePath = filePath;
    return tree;
  }

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
  [[nodiscard]] bool writeTree() noexcept;
  [[nodiscard]] static std::expected<LTree, std::string>
  getTreeFromBlob(const std::string_view objPath,
                  const std::string_view filePath,
                  const bool root = false) noexcept;
  [[nodiscard]] std::expected<std::string, std::string>
  addFile(const std::span<unsigned char> data,
          const std::filesystem::path &filePath,
          const bool isExecutable) noexcept;

  [[nodiscard]] std::expected<std::string, std::string>
  addCompressedFile(const std::span<unsigned char> data,
                    const std::filesystem::path &filePath,
                    const bool isExecutable,
                    const std::string_view hash) noexcept;
  std::expected<std::vector<unsigned char>, std::string>
  getChild(const std::filesystem::path &filePath) const noexcept;
  std::expected<std::vector<Commit>, std::string> getAllCommits() noexcept;
  const std::vector<LNode> &getChildren() const noexcept {
    return this->_children;
  }
  std::optional<LTree> getChildTree(std::string_view path) const noexcept;
  std::string getBlobSize() noexcept;
  bool commit() noexcept { return this->writeMerkleTree(); }

private:
  [[nodiscard]] bool writeBlob() noexcept;
  [[nodiscard]] bool writeHeadFile() noexcept;
  void hashTree() noexcept;
  std::vector<LNode> _children;
  bool _root = false;
};
