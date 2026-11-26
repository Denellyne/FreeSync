#define FUSE_USE_VERSION 32
#include "../FuseFS.h"
#include "../Helpers/Helpers.hpp"
#include "fuse3/fuse.h"
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <print>
#include <system_error>
#include <utility>

#define OPTION(t, p) {t, offsetof(struct options, p), 1}
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

  for (size_t i = static_cast<size_t>(offset); i < map->count; i++) {
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
        char *ip;
        int show_help;
      } options{};
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
        fuse_opt_free_args(&args);
        std::println("FreeSync Client help:");
        std::println("File-system specific options:\n"
                     "    --ip=<s>          Ip of the FreeSync server");
        return;
      }
      if (options.ip == nullptr) {
        fuse_opt_free_args(&args);
        std::println("Ip not set");
        return;
      }

      this->_pool = std::make_shared<ConnectionPool>(options.ip);
      if (!this->_pool) {
        free(options.ip);
        fuse_opt_free_args(&args);
        std::println("Unable to allocate connections");
        return;
      }
      free(options.ip);
    }

    fuse_opt_add_arg(&args, "-oauto_unmount");
    fuse_main_fn(args.argc, args.argv, &fOps, this->_pool.get());
    std::println("Exiting...");
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
