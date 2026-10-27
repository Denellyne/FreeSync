#pragma once
#include <cstring>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>
#define MAINFOLDER "./.freesync/"
#define OBJFOLDER "./.freesync/objects/"
#define UPSTREAMFILE "./.freesync/UPSTREAM"
#define HEADFILE "./.freesync/HEAD"

#define REGULAR_FILE "100000"
#define EXECUTABLE_FILE "100755"
#define SYMBOLIC_LINK "120000"
#define DIRECTORY "040000"

namespace fs = std::filesystem;
class Node {
public:
  virtual ~Node() noexcept = default;
  constexpr virtual const std::string_view getHash() const noexcept = 0;
  constexpr virtual const fs::path &getFilePath() const noexcept = 0;
  constexpr virtual const fs::path &getObjPath() const noexcept = 0;
  constexpr virtual const std::string getFileName() const noexcept = 0;
  static std::optional<std::string> getHeadFile() noexcept;

  static std::string hash(const std::vector<unsigned char> &data) noexcept;
  static std::string hash(const std::span<unsigned char> data) noexcept;
  static std::string hash(const std::string_view data) noexcept;

  // This just ensures it can be a valid path for the merkle tree, not that it
  // exists
  static std::expected<bool, std::string_view>
  validatePath(std::string_view path,
               std::vector<std::string> &context) noexcept {
    const std::string_view originalPath = path;
    if (std::find(context.cbegin(), context.cend(), originalPath) !=
        context.cend())
      return std::unexpected(
          "User is trying to map 2 different files to the same path");

    if (path.empty())
      return std::unexpected("Path is empty");
    if (!path.starts_with("/FreeSync/"))
      return std::unexpected("Path doesn't begin with /FreeSync/");
    path.remove_prefix(strlen("/FreeSync/"));

    size_t pos = path.find('/');
    while (pos != std::string_view::npos) {
      path.remove_prefix(pos + 1);
      pos = path.find('/');
    }

    if (path.empty())
      return std::unexpected("Path must end in a filename");
    context.emplace_back(originalPath);

    return true;
  }

  operator bool() const noexcept { return !this->_error; }

protected:
  fs::path _filePath = "";
  fs::path _objPath = "";
  std::string _hash = "";
  bool _error = false;
};
