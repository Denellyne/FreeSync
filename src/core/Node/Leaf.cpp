#include "Leaf.h"
#include "../Compressor/Compressor.h"
#include "Node.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <zlib.h>

Leaf::Leaf(const std::string_view filePath) {

  if (!fs::exists(filePath)) {
    std::string err = "Passed file doesn't exist ";
    err += filePath;
    err += '\n';
    throw std::runtime_error(err);
  } else if (!fs::is_regular_file(filePath)) {
    std::string err = "Passed path isn't a file ";
    err += filePath;
    err += '\n';
    throw std::runtime_error(err);
  }
  this->_filePath = filePath;
  if (const fs::perms permissions = fs::status(this->_filePath).permissions();
      (fs::perms::none != (permissions & fs::perms::owner_exec)) ||
      (fs::perms::none != (permissions & fs::perms::group_exec)) ||
      (fs::perms::none != (permissions & fs::perms::others_exec)))
    this->_isExecutable = true;

  std::vector<unsigned char> data;
  std::ifstream file(this->_filePath, std::fstream::binary);
  if (!file) {
    throw std::runtime_error("Unable to create leaf node\n");
    return;
  }

  file.seekg(0, file.end);
  const int length = file.tellg();
  data.resize(length);
  file.seekg(0, file.beg);

  file.read(reinterpret_cast<char *>(data.data()), length);

  this->_hash = this->hash(data);
  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);

  this->_objPath = path;
  if (!compressData(data))
    throw std::runtime_error("Unable to create leaf node\n");

  if (!this->writeBlob(data))
    throw std::runtime_error("Unable to write leaf blob\n");

  file.close();
}

Leaf::Leaf(const std::string_view filePath, std::vector<unsigned char> &data,
           const bool isExecutable) {

  this->_filePath = filePath;
  this->_isExecutable = isExecutable;

  this->_hash = this->hash(data);
  std::string path = OBJFOLDER;
  path.append(this->_hash, 0, 2);
  path += '/';
  path.append(this->_hash, 2, 62);

  this->_objPath = path;
  if (!compressData(data))
    throw std::runtime_error("Unable to create leaf node\n");

  if (!this->writeBlob(data))
    throw std::runtime_error("Unable to write leaf blob\n");
}
bool Leaf::writeFile(const std::string_view path,
                     const std::vector<unsigned char> &data) {

  fs::path p = path;
  fs::create_directories(p.parent_path());
  std::ofstream file(p, std::fstream::binary);
  if (!file)
    return false;

  file.write(reinterpret_cast<const char *>(data.data()), data.size());
  file.flush();
  return true;
}
bool Leaf::writeBlob(const std::vector<unsigned char> &data) {
  fs::path path = this->_objPath;
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::fstream::binary);
  if (!file)
    return false;

  file.write("blob ", 5);
  const std::string stringSize = std::to_string(data.size());
  file.write(stringSize.c_str(), stringSize.length());
  file.put('\0');
  file.write(reinterpret_cast<const char *>(data.data()), data.size());
  file.flush();
  return true;
}
std::expected<std::vector<unsigned char>, std::string> Leaf::getBlob() {
  std::vector<unsigned char> res;
  std::ifstream file(this->_objPath, std::fstream::binary);
  if (!file)
    return std::unexpected("Invalid file");

  file.seekg(0, file.end);
  int length = file.tellg();
  if (length < 6)
    return std::unexpected("Invalid file contents");
  file.seekg(5, file.beg);
  std::string size = "";
  char c = file.get();
  while (c != '\0') {
    size += c;
    c = file.get();
  }
  assert(!size.empty());
  length = std::stoul(size);

  res.resize(length);

  file.read(reinterpret_cast<char *>(res.data()), length);
  return res;
}
