#pragma once
#include "Node.h"
#include <expected>
class LTree : public Node {
private:
  struct LNode {
    std::string _entry;
    std::string _fileName;
    std::string _hash;
  };

public:
  struct Commit {
    const std::string _objPath;
    const std::filesystem::file_time_type _timestamp;
    Commit() = delete;
    Commit(const std::string_view path,
           const std::filesystem::file_time_type tm)
        : _objPath(path), _timestamp(tm) {}
  };

public:
  LTree() = default;
  LTree(const std::array<char, 64> &hash, const std::string_view filePath,
        const bool root = false);
  [[nodiscard]] bool writeMerkleTree();
  static LTree newEmptyLTree(const std::string_view filePath) {
    LTree tree = LTree();
    tree._filePath = filePath;
    return tree;
  }

  constexpr const std::string_view getHash() const override {
    return this->_hash;
  }
  constexpr const fs::path &getFilePath() const override {
    return this->_filePath;
  }
  constexpr const fs::path &getObjPath() const override {
    return this->_objPath;
  }
  constexpr const std::string getFileName() const override {
    return this->_filePath.filename().string();
  }
  [[nodiscard]] bool writeTree();
  [[nodiscard]] static std::expected<LTree, std::string>
  getTreeFromBlob(const std::string_view objPath,
                  const std::string_view filePath, const bool root = false);
  [[nodiscard]] std::expected<std::string, std::string>
  addFile(std::vector<unsigned char> &data,
          const std::filesystem::path &filePath, const bool isExecutable);
  std::expected<std::vector<Commit>, std::string> getAllCommits();

private:
  [[nodiscard]] bool writeBlob();
  [[nodiscard]] bool writeHeadFile();
  void hashTree();
  std::vector<LNode> _children;
  bool _root = false;
};
