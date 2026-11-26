#pragma once
#include <array>
#include <cstddef>
#define STORAGE_SIZE 16
#define STORAGE_ALIGNMENT 16

class FuseFS {
public:
  FuseFS(int argc, char *argv[]) noexcept;
  ~FuseFS();

  explicit operator bool() const noexcept { return !this->_error; }

  class Impl;

private:
  alignas(STORAGE_ALIGNMENT) std::array<std::byte, STORAGE_SIZE> _storage;
  Impl *getImpl() noexcept;
  bool _error = false;
};
