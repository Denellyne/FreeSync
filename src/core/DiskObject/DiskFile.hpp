#pragma once
#include "DiskObject.hpp"
#include <cassert>
#include <filesystem>
#include <unistd.h>
class DiskFile : public DiskObject {
public:
  explicit DiskFile(const bool error = false) noexcept {
    this->_error = error;
    if (this->_error)
      return;

    char TMP_TEMPLATE[] = "/var/tmp/FS_XXXXXX";
    this->_fd = mkstemp(TMP_TEMPLATE);
    this->_path = TMP_TEMPLATE;
    this->_length = 0;
    if (this->_fd < 0) {
      this->_error = true;
      this->_buf = nullptr;
      return;
    }
  }

  DiskFile &operator=(DiskFile &&other) noexcept {
    std::swap(this->_fd, other._fd);
    std::swap(this->_length, other._length);
    std::swap(this->_buf, other._buf);
    std::swap(this->_path, other._path);
    std::swap(this->_error, other._error);
    return *this;
  }
  DiskFile(DiskFile &&other) noexcept {
    std::swap(this->_fd, other._fd);
    std::swap(this->_length, other._length);
    std::swap(this->_buf, other._buf);
    std::swap(this->_path, other._path);
    std::swap(this->_error, other._error);
  }
  explicit DiskFile(std::string_view filePath,
                    const bool newFile = false) noexcept
      : _newFile(newFile) {
    if (!std::filesystem::exists(filePath) && !this->_newFile) {
      this->_error = true;
      return;
    }
    if (!this->_newFile) {
      char TMP_TEMPLATE[] = "/var/tmp/FS_XXXXXX";
      this->_fd = mkstemp(TMP_TEMPLATE);
      this->_path = TMP_TEMPLATE;
      std::filesystem::copy_file(
          filePath, this->_path,
          std::filesystem::copy_options::overwrite_existing);
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
    } else {
      this->_path = filePath;
      const std::filesystem::path parentPath =
          std::filesystem::path(this->_path).parent_path();
      if (!parentPath.empty())
        std::filesystem::create_directories(parentPath);
      this->_length = 0;
      this->_fd = open(this->_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
      if (this->_fd < 0) {
        this->_error = true;
        return;
      }
    }
  }

  ~DiskFile() noexcept override {
    if (this->_buf)
      ::munmap(this->_buf, this->_length);
    if (this->_fd >= 0) {
      this->sync();
      ::close(this->_fd);
    }
    if (!this->_newFile && !this->_path.empty())
      unlink(this->_path.c_str());
  }

  [[nodiscard]] bool write(const std::span<const unsigned char> data) noexcept {
    assert(this->_fd >= 0);
    std::size_t bytesWritten = 0;
    while (bytesWritten < data.size()) {
      const ssize_t wrote =
          ::write(this->_fd, &data[bytesWritten], data.size() - bytesWritten);
      if (wrote < 0) {
        if (errno == EINTR)
          continue;
        return false;
      }

      bytesWritten += wrote;
      this->_length += wrote;
      this->_sync += wrote;
    }
    if (this->_sync > (1024 * 1024 * 512))
      this->sync();
    return true;
  }

private:
  void sync() noexcept {
    this->_sync = 0;
    ::syncfs(this->_fd);
  }
  uint32_t _sync = 0;
  bool _newFile = false;
};
