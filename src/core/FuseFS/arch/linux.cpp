#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <future>
#include <inplace_vector>
#include <memory>
#include <optional>
#include <print>
#include <queue>
#include <ranges>
#include <system_error>
#include <unordered_map>
#define FUSE_USE_VERSION 30
#include "../../DiskObject/DiskView.hpp"
#include "../../Networking/FSClient/Fuse/FSClientFuse.h"
#include "../FuseFS.h"
#include "fuse3/fuse.h"
using SSLOpt = std::optional<SSLView>;
using SSLExp = std::expected<SSLOpt, int>;

class ConnectionPool {
#define MAX_CONNECTIONS 2
public:
  ConnectionPool() = delete;
  ConnectionPool(const std::string_view ip) noexcept {
    for (size_t i = 0; i < MAX_CONNECTIONS; i++) {
      this->_pool.unchecked_emplace_back(ip);
      this->_available.emplace(i);
    }
  }
  std::optional<size_t> getConncetion() noexcept {
    std::lock_guard lock(this->_mutex);
    if (this->_available.empty())
      return std::nullopt;

    const size_t idx = this->_available.front();
    this->_available.pop();
    return idx;
  }

  std::optional<int> downloadFile(const SSLString &command,
                                  const std::string_view outPath) noexcept {

    bool issueConnection = false;
    const std::string commandStr = command.toString();
    std::shared_future<SSLOpt> future;
    std::promise<SSLOpt> promise{};
    {
      std::lock_guard lock(this->_mutex);
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
        else
          return std::nullopt;
      }
    }
    const auto finish = [&commandStr, &promise, this](const bool sucess) {
      std::lock_guard lock(this->_mutex);
      const auto it = this->_pending.find(commandStr);
      if (it != this->_pending.end()) {
        this->_pending.erase(it);
        if (sucess)
          promise.set_value(SSLView(""));
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
      else
        return std::nullopt;
    }

    return -EIO;
  }
  SSLExp issueCommand(const SSLString &command) noexcept {

    bool issueConnection = false;
    const std::string commandStr = command.toString();
    std::shared_future<SSLOpt> future;
    std::promise<SSLOpt> promise{};
    {
      std::lock_guard lock(this->_mutex);
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
    const auto finish = [&commandStr, &promise, this](SSLOpt &&result) {
      std::lock_guard lock(this->_mutex);
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
  void pushConnectionLocked(const size_t idx) noexcept {
    this->_available.emplace(idx);
  }
  void pushConnection(const size_t idx) noexcept {
    std::lock_guard lock(this->_mutex);
    // this->_pool[idx].disconnect();
    this->_available.emplace(idx);
  }

  std::inplace_vector<FSClientFuse, MAX_CONNECTIONS> _pool;
  std::queue<size_t> _available;
  std::mutex _mutex;
  std::unordered_map<std::string, std::shared_future<SSLOpt>> _pending;
};

#define OPTION(t, p) {t, offsetof(struct options, p), 1}
struct dirEntries {
  size_t count = 0;
  char **entries;
};
void freeDir(dirEntries *map) {
  for (size_t i = 0; i < map->count; i++)
    free(map->entries[i]);

  if (map->entries)
    free(map->entries);

  free(map);
}

static int releaseDir(const char *path, struct fuse_file_info *fi) {

  (void)path;
  dirEntries *map = (dirEntries *)fi->fh;
  if (!map)
    return -EIO;
  freeDir(map);
  fi->fh = 0;
  return 0;
}

static int releaseFile(const char *path, struct fuse_file_info *fi) {
  (void)path;
  DiskView *file = (DiskView *)fi->fh;
  if (!file)
    return -EIO;
  delete file;
  fi->fh = 0;
  return 0;
}
static int openFile(const char *path, struct fuse_file_info *fi) {
  int res = 0;
  fuse_context *ctx = fuse_get_context();
  if (!ctx)
    return -EIO;
  const std::string outPath = TEMP_FOLDER + path;
  std::error_code ec;
  fi->direct_io = 1;
  if (std::filesystem::exists(outPath, ec)) {

    DiskView *file = new DiskView(outPath);
    if (!file || !(*file)) [[unlikely]]
      return -ENOMEM;
    fi->fh = (uintptr_t)(file);
    return res;
  }

  ConnectionPool &client =
      *reinterpret_cast<ConnectionPool *>(ctx->private_data);

  const SSLString command(FSCodeStr(RETR) + " /FreeSync" + path);
  if (const auto sslOpt = client.downloadFile(command, outPath);
      sslOpt.has_value()) [[unlikely]]
    return sslOpt.value();

  DiskView *file = new DiskView(outPath);
  if (!file || !(*file)) [[unlikely]]
    return -ENOMEM;
  fi->fh = (uintptr_t)(file);

  return res;
}

static int openDir(const char *path, struct fuse_file_info *fi) {

  fuse_context *ctx = fuse_get_context();
  if (!ctx) [[unlikely]]
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

    dirEntries *map = (dirEntries *)malloc(sizeof(dirEntries));
    if (!map) [[unlikely]]
      return -ENOMEM;

    map->count = 2 + strings.size();
    map->entries = (char **)(malloc(map->count * sizeof(char *)));
    if (!map->entries) [[unlikely]] {
      free(map);
      return -ENOMEM;
    }

    asprintf(&map->entries[0], ".");
    asprintf(&map->entries[1], "..");
    for (size_t i = 0; i < strings.size(); i++)
      map->entries[i + 2] = strdup(strings[i].c_str());
    fi->fh = (uintptr_t)map;
  }
  return 0;
}
static int readDir(const char *path, void *buf, fuse_fill_dir_t filler,
                   off_t offset, struct fuse_file_info *fi,
                   enum fuse_readdir_flags flags) {
  (void)flags;
  (void)path;
  dirEntries *map = (dirEntries *)fi->fh;
  if (!map)
    return -EIO;

  if (offset < 0 || (size_t)offset > map->count)
    return -EINVAL;

  for (size_t i = offset; i < map->count; i++) {
    offset = i;
    if (filler(buf, map->entries[i], NULL, (off_t)(i + 1),
               FUSE_FILL_DIR_DEFAULTS))
      break;
  }

  return 0;
}

static int getAttr(const char *path, struct stat *stbuf,
                   struct fuse_file_info *fi) {
  (void)fi;
  memset(stbuf, 0, sizeof(struct stat));
  int res = 0;
  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    return 0;
  }

  fuse_context *ctx = fuse_get_context();
  if (!ctx)
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
    if (split.size() == 0 || FSStrCode(split[0]) == ERR)
      return -ENOENT;

    if (split.size() != 4)
      return -EIO;

    if (split[1] == "EXECUTABLE")
      stbuf->st_mode = S_IFREG | 0755;
    else if (split[1] == "DIRECTORY")
      stbuf->st_mode = S_IFDIR | 0755;
    else if (split[1] == "FILE")
      stbuf->st_mode = S_IFREG | 0644;
    else
      return -EIO;
    size_t nlink = std::atoll(split[2].data());
    size_t size = std::atoll(split[3].data());
    stbuf->st_nlink = nlink;
    stbuf->st_size = size;
  }

  return res;
}
static int readFile(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi) {

  (void)path;
  DiskView *file = (DiskView *)fi->fh;
  if (!file)
    return -EIO;

  // if (strcmp(path + 1, file->getFilePath().substr(getHome()).data()) != 0)
  //   return -ENOENT;

  const std::span<const unsigned char> view = file->toSpan();
  const size_t len = view.size();
  if (offset >= 0 && (size_t)offset < len) {
    if (offset + size > len)
      size = len - offset;
    memcpy(buf, view.data() + offset, size);
  } else
    size = 0;

  return size;
}
static void *init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
  (void)conn;
  cfg->attr_timeout = 15.0;
  cfg->entry_timeout = 15.0;
  cfg->negative_timeout = 20.0;
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
          OPTION("--ip=%s", ip), OPTION("-h", show_help),
          OPTION("--help", show_help), FUSE_OPT_END};
      if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1) {
        std::println("Crash");
        return;
      }

      if (options.show_help) {
        std::println("FreeSync Client help:");
        std::println("File-system specific options:\n"
                     "    --ip=<s>          Ip of the FreeSync server");
        abort();
      } else if (options.ip == NULL) {
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
