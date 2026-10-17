#include "Tree.h"
#include "../Compressor/Compressor.h"
#include "Leaf.h"
#include "Node.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <print>

Tree::Tree(const std::string_view folder, bool root) : _root(root) {
  fs::path path = folder;
  if (!fs::exists(path)) [[unlikely]] {
    std::println("Passed folder doesn't exist {}", path);
    this->_error = true;
    return;

  } else if (!fs::is_directory(path)) [[unlikely]] {
    std::println("Passed path isn't a directory {}", path);
    this->_error = true;
    return;
  }
  this->_filePath = path;
  for (const auto &p : fs::directory_iterator(path)) {
    if (const auto &str = p.path().string();
        str.contains(".freesync") || str.contains(".git"))
      continue;

    this->_children.emplace_back(Tree::newNode(p.path().c_str()));

    if (!this->_children[this->_children.size() - 1]) [[unlikely]] {
      this->_error = true;
      return;
    }
  }

  this->hashTree();
  if (!this->writeBlob()) [[unlikely]] {
    this->_error = true;
    return;
  }
}

Tree::Tree(const std::array<char, 64> &hash, bool root) : _root(root) {
  this->_hash = std::string(hash.data());
  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);
  this->_objPath = path;
  this->_filePath = ".";
}

std::unique_ptr<Node> Tree::newNode(const std::string_view filePath) noexcept {
  if (!fs::exists(filePath)) [[unlikely]] {
    std::string err = "Passed file doesn't exist ";
    err += filePath;
    err += '\n';
    return nullptr;
  }
  if (fs::is_directory(filePath))
    return std::make_unique<Tree>(Tree(filePath));

  return std::make_unique<Leaf>(Leaf(filePath));
};

void Tree::hashTree() noexcept {
  this->_hash.reserve(this->_children.size() * 64);
  std::sort(this->_children.begin(), this->_children.end());
  for (const auto &child : this->_children)
    this->_hash += child->getHash();
  this->_hash = Node::hash(this->_hash);
  assert(this->_hash.length() == 64);

  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);
  this->_objPath = path;
}

bool Tree::writeBlob() noexcept {
  std::string data = "";
  if (this->_root) {
    if (const auto str = Node::getHeadFile(); !str.has_value()) {
      std::println("Head file is null optional");
      return false;
    } else if (str.value() == this->_hash)
      return true;
    else
      data = "PARENT:" + str.value() + '\n';
  }

  fs::path path = this->_objPath;
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::fstream::binary);
  if (!file) {
    std::println("Unable to open file to path of tree object");
    return false;
  }

  for (const auto &children : this->_children) {
    const fs::path &p = children->getFilePath();
    std::string entry = "";
    if (fs::is_directory(p))
      entry = DIRECTORY;
    else if (fs::is_regular_file(p)) {
      const Leaf *leaf = reinterpret_cast<Leaf *>(children.get());
      if (!leaf) {
        std::println("Regular file isn't of Leaf type: {}", p);
        return false;
      }
      if (leaf->isExecutable())
        entry = EXECUTABLE_FILE;
      else
        entry = REGULAR_FILE;
    }

    data += entry + ' ' + children->getFileName() + '\0' +
            children->getHash().data();
  }

  DiskFile dFile = compressData(
      std::span<unsigned char>((unsigned char *)data.data(), data.length()));
  if (!dFile)
    return false;
  file.write((char *)dFile.toSpan().data(), dFile.toSpan().size());
  file.flush();

  return true;
}

bool Tree::writeHeadFile() noexcept {
  std::ofstream file(HEADFILE, std::fstream::binary | std::fstream::trunc);
  if (!file) {
    std::println("Unable to open head file");
    return false;
  }
  file.write(this->_hash.c_str(), 64);
  return true;
}
bool Tree::writeMerkleTree() {
  if (const auto head = getHeadFile(); head.has_value() &&
                                       head.value() != "NOPARENT" &&
                                       head.value() == this->_hash)
    return true;

  return this->writeHeadFile();
}
