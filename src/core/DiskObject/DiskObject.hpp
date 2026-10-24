#pragma once
#define TEMP_FOLDER "./.tmp/"
#include <cstdint>
#include <fcntl.h>
#include <span>
#include <string>
#include <sys/mman.h>
class DiskObject {
public:
  DiskObject() noexcept = default;
  DiskObject(const DiskObject &) = delete;
  DiskObject(DiskObject &other) = delete;
  DiskObject &operator=(const DiskObject &other) = delete;
  DiskObject &operator=(DiskObject &other) = delete;
  virtual ~DiskObject() noexcept = default;
  explicit operator bool() const noexcept { return !this->_error; }
  std::span<unsigned char> toSpan() const noexcept {
    if (this->_error)
      return {};

    if (!this->_buf) {
      const_cast<DiskObject *>(this)->_buf = (unsigned char *)::mmap(
          0, this->_length, PROT_READ | PROT_WRITE, MAP_PRIVATE, this->_fd, 0);
      if (!this->_buf || this->_buf == MAP_FAILED) {
        const_cast<DiskObject *>(this)->_error = true;
        const_cast<DiskObject *>(this)->_buf = nullptr;
        return {};
      }
    }
    return std::span<unsigned char>(this->_buf, this->_length);
  }

  std::string_view getFilePath() const noexcept { return this->_path; }

protected:
  unsigned char *_buf = nullptr;
  std::string _path = "";
  uint64_t _length = 0;
  int _fd = -1;
  bool _error = false;
};
