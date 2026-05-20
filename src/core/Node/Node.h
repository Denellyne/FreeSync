#pragma once
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
  virtual ~Node() = default;
  constexpr virtual const std::string_view getHash() const = 0;
  constexpr virtual const fs::path &getFilePath() const = 0;
  constexpr virtual const fs::path &getObjPath() const = 0;
  constexpr virtual const std::string getFileName() const = 0;
  static std::optional<std::string> getHeadFile();

protected:
  static std::string hash(const std::vector<unsigned char> &data);
  static std::string hash(const std::string_view data);
  fs::path _filePath = "";
  fs::path _objPath = "";
  std::string _hash = "";
};
