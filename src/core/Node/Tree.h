#pragma once
#include "Node.h"
#include <vector>

class Tree : public Node {
public:
  explicit Tree(const std::string_view folder, bool root = false);
  explicit Tree(const std::array<char, 64> &hash, bool root = false);
  [[nodiscard]] bool writeMerkleTree();

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
  // [[nodiscard]] bool writeTree();

private:
  [[nodiscard]] bool writeBlob() noexcept;
  [[nodiscard]] bool writeHeadFile() noexcept;
  void hashTree() noexcept;
  static std::unique_ptr<Node>
  newNode(const std::string_view filePath) noexcept;
  std::vector<std::unique_ptr<Node>> _children;
  bool _root = false;
};
