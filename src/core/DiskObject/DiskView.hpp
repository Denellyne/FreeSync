#pragma once
#include "DiskObject.hpp"
#include <filesystem>

class DiskView : public DiskObject {
public:
  DiskView() noexcept { this->_error = true; }
  virtual ~DiskView() noexcept override {}

  DiskView(std::string_view filePath) noexcept {
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
      this->_error = true;
      this->_buf = nullptr;
      return;
    }
    if (this->_fd < 0) {
      ::munmap(this->_buf, this->_length);
      this->_error = true;
      this->_buf = nullptr;
      return;
    }
  }
  std::string_view getFilePath() const noexcept { return this->_path; }
};
