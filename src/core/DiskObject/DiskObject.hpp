#pragma once
#include <cstdint>
#include <fcntl.h>
#include <span>
#include <string>
#include <sys/mman.h>
static inline char const *getHome() noexcept {
  static bool initialized = false;
  static char const *home;
  if (!initialized) {
    home = getenv("HOME");
    if (home == nullptr) [[unlikely]]
      home = "/root";

    initialized = true;
  }
  return home;
}

static inline std::string getCanonicalPath(std::string_view path) noexcept {
  static char const *home = getHome();
  std::string str(home);
  str.append(path);
  return str;
}

static const std::string TEMP_FOLDER =
    getCanonicalPath("/.local/share/freesync/.tmp/");

class DiskObject {
public:
  DiskObject() noexcept = default;
  DiskObject(const DiskObject &) = delete;
  DiskObject(DiskObject &other) = delete;
  DiskObject &operator=(const DiskObject &other) = delete;
  DiskObject &operator=(DiskObject &other) = delete;
  virtual ~DiskObject() noexcept = default;
  explicit operator bool() const noexcept { return !this->_error; }
  [[nodiscard]] std::span<unsigned char> toSpan() const noexcept {
    if (this->_error)
      return {};

    if (this->_buf == nullptr) {
      const_cast<DiskObject *>(this)->_buf = static_cast<unsigned char *>(
          ::mmap(nullptr, this->_length, PROT_READ | PROT_WRITE, MAP_PRIVATE,
                 this->_fd, 0));
      if ((this->_buf == nullptr) || this->_buf == MAP_FAILED) {
        const_cast<DiskObject *>(this)->_error = true;
        const_cast<DiskObject *>(this)->_buf = nullptr;
        return {};
      }
    }
    return {this->_buf, this->_length};
  }

  [[nodiscard]] std::string_view getFilePath() const noexcept {
    return this->_path;
  }

protected:
  unsigned char *_buf = nullptr;
  std::string _path;
  uint64_t _length = 0;
  int _fd = -1;
  bool _error = false;
};
