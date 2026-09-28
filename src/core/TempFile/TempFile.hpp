#pragma once
#include <cstdio>
#include <utility>

class TempFile {
public:
  TempFile() : _file(std::tmpfile()) {}
  TempFile(const TempFile &other) = delete;
  TempFile(TempFile &other) = delete;
  TempFile &operator=(const TempFile &other) = delete;
  TempFile &operator=(TempFile &&other) noexcept {
    std::swap(this->_file, other._file);
    return *this;
  }
  TempFile(TempFile &&other) noexcept
      : _file(std::exchange(other._file, nullptr)) {}
  ~TempFile() noexcept {
    if (this->_file)
      std::fclose(this->_file);
  }
  operator bool() const { return this->_file; }
  operator FILE *() { return this->_file; }

private:
  FILE *_file = nullptr;
};
