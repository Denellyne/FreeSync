#pragma once
#include "Node.h"
#include <expected>
#include <vector>
class Leaf : public Node {
public:
  Leaf(const std::string_view filePath);
  Leaf(const std::string_view filePath, std::vector<unsigned char> &data,
       const bool isExecutable);
  Leaf(const std::string_view filePath, const std::string_view hash,
       const bool isExecutable)
      : _isExecutable(isExecutable) {
    this->_hash = hash;
    this->_filePath = filePath;
    std::string path = OBJFOLDER;
    path.append(this->_hash, 0, 2);
    path += '/';
    path.append(this->_hash, 2, 62);

    this->_objPath = path;
  }
  bool isExecutable() const { return this->_isExecutable; }

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

  std::expected<std::vector<unsigned char>, std::string> getBlob();
  [[nodiscard]] bool writeFile(const std::string_view path,
                               const std::vector<unsigned char> &data);

private:
  [[nodiscard]] bool writeBlob(const std::vector<unsigned char> &data);
  bool _isExecutable = false;
};
