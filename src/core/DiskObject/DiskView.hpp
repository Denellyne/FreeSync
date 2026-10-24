#pragma once
#include "DiskObject.hpp"
#include <filesystem>
#include <unistd.h>

class DiskView : public DiskObject {
public:
  explicit DiskView() noexcept { this->_error = true; }
  virtual ~DiskView() noexcept override {
    if (this->_buf)
      ::munmap(this->_buf, this->_length);
    if (this->_fd >= 0)
      ::close(this->_fd);
  }

  explicit DiskView(std::string_view filePath) noexcept {
    if (!std::filesystem::exists(filePath)) {
      this->_error = true;
      return;
    }
    this->_path = filePath;
    this->_fd = open(this->_path.c_str(), O_RDONLY);
    if (this->_fd < 0) {
      this->_error = true;
      return;
    }

    this->_length = std::filesystem::file_size(this->_path);

    this->_buf = (unsigned char *)::mmap(
        0, this->_length, PROT_READ | PROT_WRITE, MAP_PRIVATE, this->_fd, 0);
    if (!this->_buf || this->_buf == MAP_FAILED) {
      close(this->_fd);
      this->_fd = -1;
      this->_error = true;
      this->_buf = nullptr;
      return;
    }
  }
};
