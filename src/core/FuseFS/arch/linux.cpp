#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <future>
#include <inplace_vector>
#include <memory>
#include <optional>
#include <print>
#include <queue>
#include <ranges>
#include <system_error>
#include <unordered_map>
#include <utility>
#define FUSE_USE_VERSION 30
#include "../../DiskObject/DiskView.hpp"
#include "../../Networking/FSClient/Fuse/FSClientFuse.h"
#include "../../Node/Node.h"
#include "../FuseFS.h"
#include "fuse3/fuse.h"
using SSLOpt = std::optional<SSLView>;
using SSLExp = std::expected<SSLOpt, int>;

class ConnectionPool {
#define MAX_CONNECTIONS 8
public:
  ConnectionPool() = delete;
  constexpr ConnectionPool(const std::string_view ip) noexcept {
    for (size_t i = 0; i < MAX_CONNECTIONS; i++) {
      this->_pool.unchecked_emplace_back(ip);
      this->_available.emplace(i);
    }
  }
  constexpr std::optional<size_t> getConncetion() noexcept {
    std::scoped_lock lock(this->_mutex);
    if (this->_available.empty())
      return std::nullopt;

    const size_t idx = this->_available.front();
    this->_available.pop();
    return idx;
  }

  constexpr std::optional<int>
  downloadFile(const SSLString &command,
               const std::string_view outPath) noexcept {

    bool issueConnection = false;
    const std::string commandStr = command.toString();
    std::shared_future<SSLOpt> future;
    std::promise<SSLOpt> promise{};
    {
      std::scoped_lock lock(this->_mutex);
      auto it = this->_pending.find(commandStr);
      if (it == this->_pending.end()) {
        issueConnection = true;
        const std::shared_future<SSLOpt> fut = promise.get_future().share();
        auto [newIt, inserted] = this->_pending.emplace(commandStr, fut);
        future = newIt->second;
      } else
        future = it->second;
    }
    if (!issueConnection) {
      std::println("Waiting on {}", commandStr);
      switch (std::future_status status =
                  future.wait_for(std::chrono::minutes(15));
              status) {
      case std::future_status::deferred:
        [[fallthrough]];
      case std::future_status::timeout:
        return -EIO;
      case std::future_status::ready:
        if (const auto &opt = future.get(); !opt.has_value())
          return -EIO;
        return std::nullopt;
      }
    }
    std::println("Processing {}", commandStr);

    const auto finish = [&commandStr, &promise, this](const bool sucess) {
      std::scoped_lock lock(this->_mutex);
      const auto it = this->_pending.find(commandStr);
      if (it != this->_pending.end()) {
        this->_pending.erase(it);
        if (sucess)
          promise.set_value(SSLView(" "));
        else
          promise.set_value(std::nullopt);
      }
    };

    if (const auto idxOpt = this->getConncetion(); !idxOpt.has_value()) {
      finish(false);
      return -EAGAIN;
    } else {
      FSClientFuse &client = this->_pool[idxOpt.value()];
      if (!client.connect()) {
        finish(false);
        this->pushConnection(idxOpt.value());
        return -ENETUNREACH;
      }
      finish(client.issueCommand(command) && client.downloadFile(outPath));
      this->pushConnection(idxOpt.value());

      if (const auto &opt = future.get(); !opt.has_value())
        return -EIO;
      return std::nullopt;
    }

    return -EIO;
  }
  constexpr SSLExp issueCommand(const SSLString &command) noexcept {

    bool issueConnection = false;
    const std::string commandStr = command.toString();
    std::shared_future<SSLOpt> future;
    std::promise<SSLOpt> promise{};
    {
      std::scoped_lock lock(this->_mutex);
      auto it = this->_pending.find(commandStr);
      if (it == this->_pending.end()) {
        issueConnection = true;
        const std::shared_future<SSLOpt> fut = promise.get_future().share();
        auto [newIt, inserted] = this->_pending.emplace(commandStr, fut);
        future = newIt->second;
      } else
        future = it->second;
    }
    if (!issueConnection) {
      std::println("Waiting on {}", commandStr);
      switch (std::future_status status =
                  future.wait_for(std::chrono::seconds(15));
              status) {
      case std::future_status::deferred:
        [[fallthrough]];
      case std::future_status::timeout:
        return std::unexpected(-EIO);
      case std::future_status::ready:
        if (const auto &opt = future.get(); !opt.has_value())
          return std::unexpected(-EIO);
        else
          return opt;
      }
    }
    std::println("Processing {}", commandStr);
    const auto finish = [&commandStr, &promise, this](SSLOpt &&result) {
      std::scoped_lock lock(this->_mutex);
      const auto it = this->_pending.find(commandStr);
      if (it != this->_pending.end()) {
        this->_pending.erase(it);
        promise.set_value(result);
      }
    };
    if (const auto idxOpt = this->getConncetion(); !idxOpt.has_value()) {
      finish(std::nullopt);
      return std::unexpected(-EAGAIN);
    } else {
      FSClientFuse &client = this->_pool[idxOpt.value()];
      if (!client.connect()) {
        finish(std::nullopt);
        this->pushConnection(idxOpt.value());
        return std::unexpected(-ENETUNREACH);
      }

      finish(client.issueCommandRead(command));
      this->pushConnection(idxOpt.value());

      if (const auto &opt = future.get(); !opt.has_value())
        return std::unexpected(-EIO);
      else
        return opt;
    }

    return std::unexpected(-EIO);
  }

private:
  constexpr void pushConnectionLocked(const size_t idx) noexcept {
    this->_available.emplace(idx);
  }
  constexpr void pushConnection(const size_t idx) noexcept {
    std::scoped_lock lock(this->_mutex);
    this->_available.emplace(idx);
  }

  std::inplace_vector<FSClientFuse, MAX_CONNECTIONS> _pool;
  std::queue<size_t> _available;
  std::mutex _mutex;
  std::unordered_map<std::string, std::shared_future<SSLOpt>> _pending;
};

enum class AuxCode : uint8_t {
  OK,
  ERR,
  NOP,
};
bool addCache(const std::string &path, DiskView *file) noexcept {
  DiskFile hashCache(path + ".hash", true);
  if (!hashCache)
    return false;
  return hashCache.write(Node::hash(file->toSpan()));
}
AuxCode requestFile(const char *path, const std::string &outPath,
                    ConnectionPool &client) noexcept {
  std::error_code ec;
  if (!std::filesystem::exists(outPath + ".hash", ec))
    return AuxCode::OK;
  else if (ec)
    return AuxCode::ERR;

  if (const auto sslOpt =
          client.issueCommand(SSLString(FSCodeStr(HASH) + " /FreeSync" + path));
      !sslOpt.has_value() || !sslOpt.value().has_value()) [[unlikely]]
    return AuxCode::ERR;
  else {
    const std::string msg = sslOpt.value()->get().toString();
    const std::vector<std::string> split =
        std::string{msg.cbegin(), msg.cend()} | std::views::split(' ') |
        std::views::transform([](auto &&rg) { return std::string_view{rg}; }) |
        std::ranges::to<std::vector<std::string>>();
    if (split.empty()) [[unlikely]]
      return AuxCode::ERR;
    else if (FSStrCode(split[0]) == ERR) [[unlikely]] {
      std::println("{}", msg);
      return AuxCode::ERR;
    }

    if (DiskView file(outPath + ".hash"); !file) [[unlikely]]
      return AuxCode::ERR;
    else {
      if (const auto cachedSpan = file.toSpan();
          split[1] !=
          std::string_view(reinterpret_cast<const char *>(cachedSpan.data()),
                           cachedSpan.size()))
        return AuxCode::OK;
    }

    return AuxCode::NOP;
  }
}

#define OPTION(t, p) {t, offsetof(struct options, p), 1}
struct dirEntries {
  size_t count = 0;
  char **entries;
};
void freeDir(dirEntries *map) noexcept {
  for (size_t i = 0; i < map->count; i++)
    free(map->entries[i]);

  delete[] map->entries;

  delete map;
}

static int releaseDir(const char *path, struct fuse_file_info *fi) noexcept {

  (void)path;
  dirEntries *map = reinterpret_cast<dirEntries *>(fi->fh);
  if (map == nullptr)
    return -EIO;
  freeDir(map);
  fi->fh = 0;
  return 0;
}

static int releaseFile(const char *path, struct fuse_file_info *fi) noexcept {
  (void)path;
  DiskView *file = reinterpret_cast<DiskView *>(fi->fh);
  if (file == nullptr)
    return -EIO;
  delete file;
  fi->fh = 0;
  return 0;
}

static int openFile(const char *path, struct fuse_file_info *fi) noexcept {
  int res = 0;
  fuse_context *ctx = fuse_get_context();
  if (ctx == nullptr)
    return -EIO;
  const std::string outPath = TEMP_FOLDER + path;
  std::error_code ec;
  fi->direct_io = 1;
  ConnectionPool &client =
      *reinterpret_cast<ConnectionPool *>(ctx->private_data);

  switch (requestFile(path, outPath, client)) {
  case AuxCode::OK:
    break;
  case AuxCode::ERR:
    return -EIO;
    break;
  case AuxCode::NOP: {
    DiskView *file = new DiskView(outPath);
    if (file == nullptr) [[unlikely]]
      return -ENOMEM;
    if (!(*file)) [[unlikely]] {
      delete file;
      return -EIO;
    }
    fi->fh = reinterpret_cast<uintptr_t>(file);
    return res;
  } break;
  }

  const SSLString command(FSCodeStr(RETR) + " /FreeSync" + path);
  if (const auto sslOpt = client.downloadFile(command, outPath);
      sslOpt.has_value()) [[unlikely]]
    return sslOpt.value();

  DiskView *file = new DiskView(outPath);
  if (file == nullptr) [[unlikely]]
    return -ENOMEM;
  if (!(*file)) [[unlikely]] {
    delete file;
    return -EIO;
  }

  if (!addCache(outPath, file)) [[unlikely]] {
    delete file;
    return -EIO;
  }
  fi->fh = reinterpret_cast<uintptr_t>(file);

  return res;
}

static int openDir(const char *path, struct fuse_file_info *fi) noexcept {

  fuse_context *ctx = fuse_get_context();
  if (ctx == nullptr) [[unlikely]]
    return -EIO;

  ConnectionPool &client =
      *reinterpret_cast<ConnectionPool *>(ctx->private_data);

  const SSLString command(FSCodeStr(ITER) + " /FreeSync" + path);

  if (const auto sslOpt = client.issueCommand(command); !sslOpt.has_value())
      [[unlikely]]
    return sslOpt.error();
  else {
    std::string msg = sslOpt.value()->get().toString();
    if (msg.size() <= 4 || FSStrCode(msg) == ERR)
      return -ENOENT;

    msg = msg.substr(4);
    const std::vector<std::string> strings =
        std::string{msg.cbegin(), msg.cend()} | std::views::split('\n') |
        std::views::transform([](auto &&rg) { return std::string_view{rg}; }) |
        std::ranges::to<std::vector<std::string>>();

    dirEntries *map = new dirEntries;
    if (map == nullptr) [[unlikely]]
      return -ENOMEM;

    map->count = 2 + strings.size();
    map->entries = new char *[map->count];
    if (map->entries == nullptr) [[unlikely]] {
      delete map;
      return -ENOMEM;
    }

    asprintf(&map->entries[0], ".");
    asprintf(&map->entries[1], "..");
    for (size_t i = 0; i < strings.size(); i++)
      map->entries[i + 2] = strdup(strings[i].c_str());
    fi->fh = reinterpret_cast<uintptr_t>(map);
  }
  return 0;
}
static int readDir(const char *path, void *buf, fuse_fill_dir_t filler,
                   off_t offset, struct fuse_file_info *fi,
                   enum fuse_readdir_flags flags) noexcept {
  (void)flags;
  (void)path;
  dirEntries *map = reinterpret_cast<dirEntries *>(fi->fh);
  if (map == nullptr)
    return -EIO;

  if (offset < 0 || std::cmp_greater(offset, map->count))
    return -EINVAL;

  for (size_t i = offset; i < map->count; i++) {
    offset = static_cast<int>(i);
    if (filler(buf, map->entries[i], nullptr, static_cast<off_t>(i + 1),
               FUSE_FILL_DIR_DEFAULTS) != 0)
      break;
  }

  return 0;
}

static int getAttr(const char *path, struct stat *stbuf,
                   struct fuse_file_info *fi) noexcept {
  (void)fi;
  memset(stbuf, 0, sizeof(struct stat));
  int res = 0;
  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    return 0;
  }

  fuse_context *ctx = fuse_get_context();
  if (ctx == nullptr)
    return -EIO;

  ConnectionPool &client =
      *reinterpret_cast<ConnectionPool *>(ctx->private_data);

  const SSLString command(FSCodeStr(ATTR) + " /FreeSync" + path);
  if (const auto sslOpt = client.issueCommand(command); !sslOpt.has_value())
      [[unlikely]]
    return sslOpt.error();
  else {
    const std::string msg = sslOpt.value()->get().toString();
    const std::vector<std::string> split =
        std::string{msg.cbegin(), msg.cend()} | std::views::split(' ') |
        std::views::transform([](auto &&rg) { return std::string_view{rg}; }) |
        std::ranges::to<std::vector<std::string>>();
    if (split.empty() || FSStrCode(split[0]) == ERR)
      return -ENOENT;

    if (split.size() != 5)
      return -EIO;

    if (split[1] == "EXECUTABLE")
      stbuf->st_mode = S_IFREG | 0755;
    else if (split[1] == "DIRECTORY")
      stbuf->st_mode = S_IFDIR | 0755;
    else if (split[1] == "FILE")
      stbuf->st_mode = S_IFREG | 0644;
    else
      return -EIO;
    const size_t nlink = std::atoll(split[2].data());
    const size_t size = std::atoll(split[3].data());
    const size_t modificationDate = std::atoll(split[4].data());
    stbuf->st_nlink = nlink;
    stbuf->st_size = static_cast<__off_t>(size);
    stbuf->st_mtim.tv_nsec = modificationDate % 1'000'000'000LL;
    stbuf->st_mtim.tv_sec = modificationDate / 1'000'000'000LL;

    return res;
  }
}
static int readFile(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi) noexcept {

  (void)path;
  DiskView *file = reinterpret_cast<DiskView *>(fi->fh);
  if (file == nullptr)
    return -EIO;

  // if (strcmp(path + 1, file->getFilePath().substr(getHome()).data()) != 0)
  //   return -ENOENT;

  const std::span<const unsigned char> view = file->toSpan();
  const size_t len = view.size();
  if (offset >= 0 && std::cmp_less(offset, len)) {
    if (offset + size > len)
      size = len - offset;
    memcpy(buf, view.data() + offset, size);
  } else
    size = 0;

  return static_cast<int>(size);
}
static void *init(struct fuse_conn_info *conn,
                  struct fuse_config *cfg) noexcept {
  (void)conn;
  cfg->attr_timeout = 5.0;
  cfg->entry_timeout = 5.0;
  cfg->auto_cache = 1;
  cfg->negative_timeout = 5.0;
  return fuse_get_context()->private_data;
}
static const fuse_operations fOps = {
    .getattr = getAttr,
    .open = openFile,
    .read = readFile,
    .release = releaseFile,
    .opendir = openDir,
    .readdir = readDir,
    .releasedir = releaseDir,
    .init = init,
};

class FuseFS::Impl {
public:
  Impl(int argc, char *argv[]) {
    fuse_args args = FUSE_ARGS_INIT(argc, argv);
    {
      struct options {
        const char *ip;
        int show_help;
      } options;
      static const struct fuse_opt option_spec[] = {
          OPTION("--ip=%s", ip),
          OPTION("-h", show_help),
          OPTION("--help", show_help),
          FUSE_OPT_END,
      };
      if (fuse_opt_parse(&args, &options, option_spec, nullptr) == -1) {
        std::println("Crash");
        return;
      }

      if (options.show_help != 0) {
        std::println("FreeSync Client help:");
        std::println("File-system specific options:\n"
                     "    --ip=<s>          Ip of the FreeSync server");
        return;
      }
      if (options.ip == nullptr) {
        std::println("Ip not set");
        return;
      }

      this->_pool = std::make_shared<ConnectionPool>(options.ip);
      if (!this->_pool) {
        std::println("Unable to allocate connections");
        return;
      }
    }

    fuse_opt_add_arg(&args, "-oauto_unmount");
    fuse_main_fn(args.argc, args.argv, &fOps, this->_pool.get());
    fuse_opt_free_args(&args);
  }

private:
  std::shared_ptr<ConnectionPool> _pool;
};

FuseFS::FuseFS(int argc, char *argv[]) noexcept {

  static_assert(sizeof(FuseFS::Impl) <= STORAGE_SIZE,
                "ImplSize is too small for PIMPL");
  static_assert(alignof(FuseFS::Impl) <= STORAGE_ALIGNMENT,
                "ImplAlign is insufficient for PIMPL");
  new (&_storage) Impl(argc, argv);
}

FuseFS::~FuseFS() { std::destroy_at(getImpl()); }
FuseFS::Impl *FuseFS::getImpl() noexcept {
  return std::launder(reinterpret_cast<FuseFS::Impl *>(&this->_storage));
}
