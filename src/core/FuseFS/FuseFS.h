#pragma once
#include <cstddef>
#define STORAGE_SIZE 280
#define STORAGE_ALIGNMENT 16

class FuseFS {
public:
  FuseFS(int argc, char *argv[]) noexcept;
  ~FuseFS();

  explicit operator bool() const noexcept { return !this->_error; }

  class Impl;

private:
  alignas(STORAGE_ALIGNMENT) std::byte _storage[STORAGE_SIZE];
  Impl *getImpl() noexcept;
  bool _error = false;
};
