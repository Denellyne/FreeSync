#pragma once
#include "../../Node/LTree.h"
#include <string>
class Filesystem {
public:
  Filesystem(const std::string &);

private:
  const std::string &_currentPath;
  LTree _node;
};
