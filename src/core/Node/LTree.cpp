#include "LTree.h"
#include "../Compressor/Compressor.h"
#include "Leaf.h"
#include "Node.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

LTree::LTree(const std::array<char, 64> &hash, const std::string_view filePath,
             const bool root)
    : _root(root) {
  this->_hash = std::string(hash.data());

  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);
  this->_objPath = path;
  if (const auto val = getTreeFromBlob(this->_objPath.c_str(), filePath);
      val.has_value()) {
    this->_children = val->_children;
    this->_filePath = val->_filePath;
  } else
    throw std::runtime_error(val.error());
}

std::expected<LTree, std::string>
LTree::getTreeFromBlob(const std::string_view objPath,
                       const std::string_view filePath, const bool root) {
  std::vector<unsigned char> data;
  LTree tree;
  tree._root = root;
  tree._objPath = objPath;
  tree._filePath = filePath;
  std::ifstream file(tree._objPath, std::fstream::binary);
  if (!file)
    return std::unexpected(std::string("Unable to open file\n"));

  file.seekg(0, file.end);
  const int length = file.tellg();
  data.resize(length);
  file.seekg(0, file.beg);
  file.read(reinterpret_cast<char *>(data.data()), length);
  if (!decompressData(data))
    return std::unexpected("Unable to decompress data\n");

  std::string dataStr =
      std::string(reinterpret_cast<char *>(data.data()), data.size());
  if (dataStr.contains("PARENT:"))
    dataStr.erase(0, dataStr.find_first_of('\n') + 1);
  else
    return std::unexpected(std::string("Invalid tree blob\n"));

  while (!dataStr.empty()) {
    const std::string entry = dataStr.substr(0, 6);
    dataStr.erase(0, 7);
    const size_t fileNameIdx = dataStr.find_first_of('\0', 0);
    const std::string fileName = dataStr.substr(0, fileNameIdx);
    dataStr.erase(0, fileName.length() + 1);
    const std::string hash = dataStr.substr(0, 64);
    dataStr.erase(0, 64);
    tree._children.emplace_back(LNode{entry, fileName, hash});
  }
  tree.hashTree();

  return tree;
}

void LTree::hashTree() {
  this->_hash.reserve(this->_children.size() * 64);
  for (const auto &child : this->_children)
    this->_hash += child._hash.data();
  this->_hash = Node::hash(this->_hash);
  assert(this->_hash.length() == 64);

  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);
  this->_objPath = path;
}

bool LTree::writeBlob() {
  fs::path path = this->_objPath;
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::fstream::binary);
  if (!file) {
    std::cerr << "Unable to open file to path of tree object\n";
    return false;
  }
  std::string data = "";
  if (this->_root) {
    if (const auto str = Node::getHeadFile(); !str.has_value()) {
      std::cerr << "Head file is null optional\n";
      return false;
    } else
      data = "PARENT:" + str.value() + '\n';
  }

  for (const auto &children : this->_children)
    data += children._entry + ' ' + children._fileName + '\0' + children._hash;

  if (!compressData(data))
    return false;
  file.write(data.data(), data.size());

  return true;
}

bool LTree::writeHeadFile() {
  std::ofstream file(HEADFILE, std::fstream::binary | std::fstream::trunc);
  if (!file) {
    std::cerr << "Unable to open head file\n";
    return false;
  }
  file.write(this->_hash.c_str(), 64);
  return true;
}
bool LTree::writeMerkleTree() {
  if (const auto head = getHeadFile(); head.has_value() &&
                                       head.value() != "NOPARENT" &&
                                       head.value() == this->_hash)
    return true;

  return this->writeHeadFile();
}
bool LTree::writeTree() {

  std::cout << "\nTree: " << this->_filePath << '\n';
  for (const auto &child : this->_children) {
    std::cout << "Child: " << child._fileName << ' ';
    const std::string filePath =
        this->_filePath.string() + '/' + child._fileName;
    if (child._entry == DIRECTORY) {
      std::string obj = OBJFOLDER;
      obj.append(child._hash, 0, 2);
      obj += '/';
      obj.append(child._hash, 2, 62);
      if (auto tree = getTreeFromBlob(obj, filePath); tree.has_value()) {
        if (!tree.value().writeTree())
          return false;
      } else
        return false;
    } else {
      bool isExecutable = false;
      if (child._entry == EXECUTABLE_FILE)
        isExecutable = true;
      Leaf leaf = Leaf(filePath, child._hash, isExecutable);
      if (auto data = leaf.getBlob(); data.has_value()) {
        std::vector<unsigned char> &vec = data.value();
        if (!decompressData(vec) ||
            !leaf.writeFile(leaf.getFilePath().string(), vec))
          return false;
      } else
        return false;
    }
  }
  std::cout << '\n';

  return true;
}

std::expected<std::string, std::string>
LTree::addFile(std::vector<unsigned char> &data,
               const std::filesystem::path &filePath, const bool isExecutable) {

  constexpr auto isSubPath = [](const fs::path &path, const fs::path &base) {
    const auto mismatchPair =
        std::mismatch(path.begin(), path.end(), base.begin(), base.end());
    return mismatchPair.second == base.end();
  };

  if (filePath.parent_path() == this->_filePath) {
    const std::string fileName = filePath.filename();
    for (auto it = this->_children.begin(); it != this->_children.end(); it++)
      if ((*it)._fileName == fileName) {
        this->_children.erase(it);
        break;
      }

    const Leaf leaf = Leaf(filePath.string(), data, isExecutable);
    std::string entry = REGULAR_FILE;
    if (isExecutable)
      entry = EXECUTABLE_FILE;

    this->_children.emplace_back(
        LNode(entry, leaf.getFileName(), leaf.getHash().data()));
  } else if (isSubPath(filePath, this->_filePath)) {
    bool newDirectory = true;
    for (auto it = this->_children.begin(); it != this->_children.end(); it++) {
      if ((*it)._entry == DIRECTORY) {
        const std::string p = this->_filePath.string() + '/' + (*it)._fileName;
        if (isSubPath(filePath, p)) {
          std::string obj = OBJFOLDER;

          obj.append((*it)._hash, 0, 2);
          obj += '/';
          obj.append((*it)._hash, 2, 62);
          if (auto tree = getTreeFromBlob(obj, p); tree.has_value()) {
            if (const auto newHash =
                    tree.value().addFile(data, filePath, isExecutable);
                !newHash.has_value())
              return std::unexpected(newHash.error());
          } else
            return std::unexpected(tree.error());

          newDirectory = false;
          break;
        }
      }
    }
    if (newDirectory) {
      std::string treePath = this->_filePath.string() + '/';

      const std::string subDirectory =
          filePath.string().substr(treePath.length());
      const int pos = subDirectory.find_first_of('/');
      treePath += subDirectory.substr(0, pos);

      LTree tree = LTree::newEmptyLTree(treePath);
      const auto hash = tree.addFile(data, filePath, isExecutable);
      if (!hash.has_value() || !tree.writeBlob())
        return std::unexpected(hash.error());
      this->_children.emplace_back(
          LNode{DIRECTORY, tree.getFileName(), tree._hash});
    }
  }

  this->hashTree();
  if (!this->writeBlob())
    return std::unexpected("Unable to save tree blob\n");

  return this->_hash;
}

std::expected<std::vector<LTree::Commit>, std::string> LTree::getAllCommits() {
  const auto head = getHeadFile();
  if (!head.has_value())
    return {};

  std::string obj = OBJFOLDER;

  obj.append(head.value(), 0, 2);
  obj += '/';
  obj.append(head.value(), 2, 62);
  std::vector<LTree::Commit> commits = {
      LTree::Commit(obj, std::filesystem::last_write_time(obj))};
  while (true) {
    obj = OBJFOLDER;
    std::vector<unsigned char> data;

    std::ifstream file((commits.cend() - 1)->_objPath, std::fstream::binary);
    if (!file)
      return std::unexpected(std::string("Unable to open file\n"));

    file.seekg(0, file.end);
    const int length = file.tellg();
    data.resize(length);
    file.seekg(0, file.beg);
    file.read(reinterpret_cast<char *>(data.data()), length);
    if (!decompressData(data))
      return std::unexpected("Unable to decompress data\n");

    const std::string dataStr =
        std::string(reinterpret_cast<char *>(data.data()), data.size());

    if (dataStr.contains("NOPARENT"))
      break;
    else if (dataStr.contains("PARENT:")) {
      const std::string hash = dataStr.substr(7, 64);
      obj.append(hash, 0, 2);
      obj += '/';
      obj.append(hash, 2, 62);
      commits.emplace_back(
          LTree::Commit(obj, std::filesystem::last_write_time(obj)));
    } else
      return std::unexpected(std::string("Invalid tree blob\n"));
  }
  return commits;
}
