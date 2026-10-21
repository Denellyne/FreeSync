#include "LTree.h"
#include "../Compressor/Compressor.h"
#include "Leaf.h"
#include "Node.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <print>
#include <vector>

LTree::LTree(const std::array<char, 64> &hash, const std::string_view filePath,
             const bool root) noexcept
    : _root(root) {
  this->_hash = std::string(hash.data(), 64);
  this->_filePath = filePath;

  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);
  this->_objPath = path;
  if (const auto val =
          getTreeFromBlob(this->_objPath.c_str(), this->_filePath.c_str());
      val.has_value()) {
    this->_children = std::move(val->_children);
  } else [[unlikely]]
    this->_error = true;
}

std::expected<LTree, std::string>
LTree::getTreeFromBlob(const std::string_view objPath,
                       const std::string_view filePath,
                       const bool root) noexcept {
  LTree tree;
  tree._root = root;
  tree._objPath = objPath;
  tree._filePath = filePath;
  Decompressor decomp(tree._objPath.string());
  std::span<unsigned char> data = decomp.getData();

  std::string dataStr =
      std::string(reinterpret_cast<const char *>(data.data()), data.size());
  if (dataStr.starts_with("PARENT:")) {
    const size_t newline = dataStr.find('\n');

    if (newline == std::string::npos)
      return std::unexpected("Missing new line after Parent header");

    dataStr.erase(0, newline + 1);
  }
  // else
  //   return std::unexpected(std::string("Invalid tree blob\n"));

  while (!dataStr.empty()) {
    const std::string entry(dataStr.substr(0, 6));
    dataStr.erase(0, 7);
    const size_t fileNameIdx = dataStr.find('\0');
    if (fileNameIdx == std::string::npos)
      return std::unexpected("Unabled to find filename delimiter");
    const std::string fileName(dataStr.substr(0, fileNameIdx));
    dataStr.erase(0, fileName.length() + 1);
    if (dataStr.size() < 64) {
      std::println("{}", dataStr);
      return std::unexpected("DataStr doesn't have the length of a hash");
    }
    const std::string hash(dataStr.substr(0, 64));
    dataStr.erase(0, 64);
    tree._children.emplace_back(LNode{entry, fileName, hash});
  }
  tree.hashTree();

  return tree;
}

void LTree::hashTree() noexcept {
  std::sort(this->_children.begin(), this->_children.end());
  this->_hash.clear();
  this->_hash.reserve(this->_children.size() * 64);
  for (const auto &child : this->_children)
    this->_hash += child._hash;
  this->_hash = Node::hash(this->_hash);
  assert(this->_hash.length() == 64);

  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);
  this->_objPath = path;
}

bool LTree::writeBlob() noexcept {
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

  DiskFile dFile = compressData(
      std::span<unsigned char>((unsigned char *)data.data(), data.length()));
  if (!dFile)
    return false;
  file.write((char *)dFile.toSpan().data(), dFile.toSpan().size());

  return true;
}

bool LTree::writeHeadFile() noexcept {
  std::ofstream file(HEADFILE, std::fstream::binary | std::fstream::trunc);
  if (!file) {
    std::cerr << "Unable to open head file\n";
    return false;
  }
  file.write(this->_hash.c_str(), 64);
  return true;
}
bool LTree::writeMerkleTree() noexcept {
  if (const auto head = getHeadFile(); head.has_value() &&
                                       head.value() != "NOPARENT" &&
                                       head.value() == this->_hash)
    return true;

  return this->writeHeadFile();
}
bool LTree::writeTree() noexcept {

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
        Decompressor decomp(leaf.getFilePath().string());
        if (!leaf.writeFile(leaf.getFilePath().string(), decomp.getData()))
          return false;
      } else
        return false;
    }
  }
  std::cout << '\n';

  return true;
}

std::expected<DiskFile, std::string>
LTree::getChildFile(const std::filesystem::path &filePath) const noexcept {

  constexpr auto isSubPath = [](const fs::path &path, const fs::path &base) {
    const auto mismatchPair =
        std::mismatch(path.begin(), path.end(), base.begin(), base.end());
    return mismatchPair.second == base.end();
  };

  if (filePath.parent_path() == this->_filePath) {
    const std::string fileName = filePath.filename();
    for (auto it = this->_children.begin(); it != this->_children.end(); it++)
      if ((*it)._fileName == fileName) {
        Leaf l(filePath.string(), it->_hash, it->_entry == EXECUTABLE_FILE);
        if (auto finalOpt = l.getFinalDecompressFile(); !finalOpt.has_value())
          return std::unexpected(finalOpt.error());
        else
          return std::move(finalOpt.value());
      }

  } else if (isSubPath(filePath, this->_filePath)) {
    for (auto it = this->_children.begin(); it != this->_children.end(); it++) {
      if ((*it)._entry == DIRECTORY) {
        const std::string p = this->_filePath.string() + '/' + (*it)._fileName;
        if (isSubPath(filePath, p)) {
          std::string obj = OBJFOLDER;

          obj.append((*it)._hash, 0, 2);
          obj += '/';
          obj.append((*it)._hash, 2, 62);
          if (auto tree = getTreeFromBlob(obj, p); tree.has_value())
            return tree->getChildFile(filePath);
          else
            return std::unexpected(tree.error());
        }
      }
    }
  }
  return std::unexpected("Unable to find file");
}
std::expected<std::vector<unsigned char>, std::string>
LTree::getChild(const std::filesystem::path &filePath) const noexcept {

  constexpr auto isSubPath = [](const fs::path &path, const fs::path &base) {
    const auto mismatchPair =
        std::mismatch(path.begin(), path.end(), base.begin(), base.end());
    return mismatchPair.second == base.end();
  };

  if (filePath.parent_path() == this->_filePath) {
    const std::string fileName = filePath.filename();
    for (auto it = this->_children.begin(); it != this->_children.end(); it++)
      if ((*it)._fileName == fileName) {
        Leaf l(filePath.string(), it->_hash, it->_entry == EXECUTABLE_FILE);
        return l.getFinalDecompressBlob();
      }

  } else if (isSubPath(filePath, this->_filePath)) {
    for (auto it = this->_children.begin(); it != this->_children.end(); it++) {
      if ((*it)._entry == DIRECTORY) {
        const std::string p = this->_filePath.string() + '/' + (*it)._fileName;
        if (isSubPath(filePath, p)) {
          std::string obj = OBJFOLDER;

          obj.append((*it)._hash, 0, 2);
          obj += '/';
          obj.append((*it)._hash, 2, 62);
          if (auto tree = getTreeFromBlob(obj, p); tree.has_value())
            return tree->getChild(filePath);
          else
            return std::unexpected(tree.error());
        }
      }
    }
  }
  return std::unexpected("Unable to find file");
}

std::expected<std::string, std::string>
LTree::removeFile(const std::filesystem::path &filePath) noexcept {
  constexpr auto isSubPath = [](const fs::path &path, const fs::path &base) {
    const auto mismatchPair =
        std::mismatch(path.begin(), path.end(), base.begin(), base.end());
    return mismatchPair.second == base.end();
  };
  if (filePath.empty())
    return std::unexpected("Passed file is empty");

  if (filePath.parent_path() == this->_filePath) {
    const std::string fileName = filePath.filename();
    for (auto it = this->_children.begin(); it != this->_children.end(); it++)
      if ((*it)._fileName == fileName) {
        this->_children.erase(it);
        break;
      }
  } else if (isSubPath(filePath, this->_filePath)) {
    for (auto it = this->_children.begin(); it != this->_children.end(); it++) {
      if ((*it)._entry == DIRECTORY) {
        const std::string p = this->_filePath.string() + '/' + (*it)._fileName;
        if (isSubPath(filePath, p)) {
          std::string obj = OBJFOLDER;
          obj.append((*it)._hash, 0, 2);
          obj += '/';
          obj.append((*it)._hash, 2, 62);
          if (auto tree = getTreeFromBlob(obj, p); tree.has_value()) {
            if (const auto newHash = tree.value().removeFile(filePath);
                !newHash.has_value())
              return std::unexpected(newHash.error());
            else
              it->_hash = newHash.value();
          } else
            return std::unexpected(tree.error());
          break;
        }
      }
    }
  }

  this->hashTree();
  if (!this->writeBlob())
    return std::unexpected("Unable to save tree blob\n");

  return this->_hash;
}

std::expected<std::string, std::string> LTree::addCompressedFile(
    const std::span<unsigned char> data, const std::filesystem::path &filePath,
    const bool isExecutable, const std::string_view hash) noexcept {

  constexpr auto isSubPath = [](const fs::path &path, const fs::path &base) {
    const auto mismatchPair =
        std::mismatch(path.begin(), path.end(), base.begin(), base.end());
    return mismatchPair.second == base.end();
  };
  if (hash.empty() || hash == "")
    return std::unexpected("Hash is empty");

  if (filePath.parent_path() == this->_filePath) {
    const std::string fileName = filePath.filename();
    bool newFile = true;
    for (auto it = this->_children.begin(); it != this->_children.end(); it++)
      if ((*it)._fileName == fileName) {
        if (it->_hash == hash)
          return this->_hash;
        newFile = false;
        Leaf leaf(filePath.string(), it->_hash, it->_entry == EXECUTABLE_FILE);
        if (!leaf)
          return std::unexpected("Unable to find leaf");

        if (const auto dataOpt = leaf.diffFile(data, true);
            !dataOpt.has_value())
          return std::unexpected(dataOpt.error());
        else {
          std::vector<unsigned char> diffs = dataOpt.value();
          if (diffs.empty())
            return this->_hash;
          Leaf newLeaf(filePath.string(), diffs, it->_hash, isExecutable);
          if (!newLeaf)
            return std::unexpected("Unable to allocate new leaf");

          this->_children.erase(it);
          std::string entry = REGULAR_FILE;
          if (isExecutable)
            entry = EXECUTABLE_FILE;
          this->_children.emplace_back(LNode{entry, newLeaf.getFileName(),
                                             std::string(newLeaf.getHash())});
        }
        break;
      }
    if (newFile) {
      const Leaf leaf = Leaf(filePath.string(), data, isExecutable, hash);
      if (!leaf)
        return std::unexpected("Unable to allocate new leaf");
      std::string entry = REGULAR_FILE;
      if (isExecutable)
        entry = EXECUTABLE_FILE;

      this->_children.emplace_back(
          LNode(entry, leaf.getFileName(), leaf.getHash().data()));
    }

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
            if (const auto newHash = tree.value().addCompressedFile(
                    data, filePath, isExecutable, hash);
                !newHash.has_value())
              return std::unexpected(newHash.error());
            else {
              it->_hash = newHash.value();
            }
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
      const auto treeHash =
          tree.addCompressedFile(data, filePath, isExecutable, hash);
      if (!treeHash.has_value() || !tree.writeBlob())
        return std::unexpected(treeHash.error());
      this->_children.emplace_back(
          LNode{DIRECTORY, tree.getFileName(), tree._hash});
    }
  }

  this->hashTree();
  if (!this->writeBlob())
    return std::unexpected("Unable to save tree blob\n");

  return this->_hash;
}
std::expected<std::string, std::string>
LTree::addFile(const std::span<unsigned char> data,
               const std::filesystem::path &filePath,
               const bool isExecutable) noexcept {

  constexpr auto isSubPath = [](const fs::path &path, const fs::path &base) {
    const auto mismatchPair =
        std::mismatch(path.begin(), path.end(), base.begin(), base.end());
    return mismatchPair.second == base.end();
  };

  if (filePath.parent_path() == this->_filePath) {
    const std::string fileName = filePath.filename();
    bool newFile = true;
    for (auto it = this->_children.begin(); it != this->_children.end(); it++)
      if ((*it)._fileName == fileName) {
        newFile = false;
        Leaf leaf(filePath.string(), it->_hash, it->_entry == EXECUTABLE_FILE);
        if (!leaf)
          return std::unexpected("Unable to find leaf");

        if (const auto dataOpt = leaf.diffFile(data, false);
            !dataOpt.has_value())
          return std::unexpected(dataOpt.error());
        else {
          std::vector<unsigned char> diffs = dataOpt.value();
          if (diffs.empty())
            return this->_hash;
          Leaf newLeaf(filePath.string(), diffs, it->_hash, isExecutable);
          if (!newLeaf)
            return std::unexpected("Unable to allocate new leaf");

          this->_children.erase(it);
          std::string entry = REGULAR_FILE;
          if (isExecutable)
            entry = EXECUTABLE_FILE;
          this->_children.emplace_back(LNode{entry, newLeaf.getFileName(),
                                             std::string(newLeaf.getHash())});
        }
        break;
      }
    if (newFile) {
      const Leaf leaf = Leaf(filePath.string(), data, isExecutable);
      if (!leaf)
        return std::unexpected("Unable to allocate new leaf");
      std::string entry = REGULAR_FILE;
      if (isExecutable)
        entry = EXECUTABLE_FILE;

      this->_children.emplace_back(
          LNode(entry, leaf.getFileName(), leaf.getHash().data()));
    }

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
            else {
              it->_hash = newHash.value();
            }
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

std::expected<std::vector<LTree::Commit>, std::string>
LTree::getAllCommits() noexcept {
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

    Decompressor decomp((commits.cend() - 1)->_objPath);
    const std::span<unsigned char> data = decomp.getData();

    const std::string_view dataStr =
        std::string_view(reinterpret_cast<char *>(data.data()), data.size());

    if (dataStr.contains("NOPARENT"))
      break;
    else if (dataStr.contains("PARENT:")) {
      const std::string hash = std::string(dataStr.substr(7, 64));
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
std::optional<LTree> LTree::getChildTree(std::string_view path) const noexcept {
  if (!path.find(' '))
    path.remove_prefix(1);
  if (!path.find('/'))
    path.remove_prefix(1);
  if (path.empty()) {
    std::println("Path is empty");
    return std::nullopt;
  }

  alignas(32) std::array<char, 64> hash;
  const std::string treeName = this->getFileName();
  if (path == treeName) {
    memcpy(hash.data(), this->_hash.data(), 64);
    return LTree(hash, this->_filePath.string(), this->_root);
  } else if (const size_t pos = path.find(treeName + '/');
             pos != std::string::npos)
    path.remove_prefix(treeName.length() + 1);
  else {
    std::println("Path is an invalid directory path");
    return std::nullopt;
  }

  const std::string_view folder = path.substr(0, path.find('/'));
  for (const auto &child : this->_children)
    if (child._entry == DIRECTORY && child._fileName == folder) {
      memcpy(hash.data(), child._hash.data(), 64);
      const LTree tree = LTree(hash, treeName + '/' + child._fileName, false);
      if (!tree)
        return std::nullopt;
      return tree.getChildTree(path);
    }

  return std::nullopt;
}
unsigned LTree::LNode::getFileSize() const noexcept {
  assert(!this->_hash.empty());
  if (this->_entry == DIRECTORY)
    return 0;
  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);
  return std::filesystem::file_size(path);
}
