#pragma once
#include "Node.h"
#include <vector>

class Tree : public Node {
public:
  Tree(const std::string_view folder, bool root = false);
  Tree(const std::array<char, 64> &hash, bool root = false);
  [[nodiscard]] bool writeMerkleTree();

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
  // [[nodiscard]] bool writeTree();

private:
  [[nodiscard]] bool writeBlob();
  [[nodiscard]] bool writeHeadFile();
  void hashTree();
  static std::unique_ptr<Node> newNode(const std::string_view filePath);
  std::vector<std::unique_ptr<Node>> _children;
  bool _root = false;
};
