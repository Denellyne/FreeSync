#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <print>
#define FUSE_USE_VERSION 30
#include "../../Networking/FSClient/FSClient.h"
#include "../FuseFS.h"
#include "fuse3/fuse.h"
std::mutex m;

#define OPTION(t, p) {t, offsetof(struct options, p), 1}
struct dirEntries {
  size_t count = 0;
  char **entries;
};
void freeDir(dirEntries *map) {
  for (size_t i = 0; i < map->count; i++)
    free(map->entries[i]);
  free(map->entries);
  free(map);
}

static int releasedir(const char *path, struct fuse_file_info *fi) {

  dirEntries *map = (dirEntries *)fi->fh;
  if (!map)
    return -EIO;
  freeDir(map);
  fi->fh = 0;
  return 0;
}
static int opendir(const char *path, struct fuse_file_info *fi) {
  std::lock_guard lock(m);
  dirEntries *map = (dirEntries *)malloc(sizeof(dirEntries));
  if (!map) [[unlikely]]
    return -ENOMEM;

  fuse_context *ctx = fuse_get_context();
  if (!ctx) [[unlikely]] {
    freeDir(map);
    fi->fh = 0;
    return -EIO;
  }

  FSClient *client = reinterpret_cast<FSClient *>(ctx->private_data);
  if (!client->connect()) [[unlikely]] {
    freeDir(map);
    fi->fh = 0;
    return -ENETUNREACH;
  }
  const SSLString command(FSCodeStr(ITER) + " /FreeSync" + path);
  if (!client->validateCommand(command).has_value()) [[unlikely]] {
    freeDir(map);
    fi->fh = 0;
    return -EIO;
  }
  if (const auto sslOpt = client->issueCommand(command); !sslOpt.has_value())
      [[unlikely]] {
    freeDir(map);
    fi->fh = 0;
    return -EIO;
  } else {
    std::string msg = sslOpt->toString();
    if (msg.size() <= 4 || FSStrCode(msg) == ERR) {
      freeDir(map);
      fi->fh = 0;
      return -ENOENT;
    }
    msg = msg.substr(4);
    const std::vector<std::string> strings =
        std::string{msg.cbegin(), msg.cend()} | std::views::split('\n') |
        std::views::transform([](auto &&rg) { return std::string_view{rg}; }) |
        std::ranges::to<std::vector<std::string>>();

    map->count = 2 + strings.size();
    map->entries = (char **)(malloc(map->count * sizeof(char *)));
    asprintf(&map->entries[0], ".");
    asprintf(&map->entries[1], "..");
    for (size_t i = 0; i < strings.size(); i++)
      map->entries[i + 2] = strdup(strings[i].c_str());
    fi->fh = (uintptr_t)map;
  }
  return 0;
}
static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
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

static int hello_getattr(const char *path, struct stat *stbuf,
                         struct fuse_file_info *fi) {
  std::lock_guard lock(m);
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

  FSClient *client = reinterpret_cast<FSClient *>(ctx->private_data);
  if (!client->connect())
    return -ENETUNREACH;
  const SSLString command(FSCodeStr(ATTR) + " /FreeSync" + path);
  if (!client->validateCommand(command).has_value()) [[unlikely]]
    return -EIO;
  if (const auto sslOpt = client->issueCommand(command); !sslOpt.has_value())
      [[unlikely]]
    return -EIO;
  else {
    const std::string msg = sslOpt->toString();
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
      stbuf->st_mode = S_IFREG | 0444;
    else
      return -EIO;
    size_t nlink = std::atoll(split[2].data());
    size_t size = std::atoll(split[3].data());
    stbuf->st_nlink = nlink;
    stbuf->st_size = size;
  }
  // } else if (strcmp(path + 1, filename) == 0) {
  //   stbuf->st_mode = S_IFREG | 0444;
  //   stbuf->st_nlink = 1;
  //   stbuf->st_size = 5;
  // } else
  //   res = -ENOENT;

  return res;
}
static const fuse_operations fOps = {.getattr = hello_getattr,
                                     .opendir = opendir,
                                     .readdir = hello_readdir,
                                     .releasedir = releasedir};
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
        abort(); // TEMP
      }

      if (options.show_help) {
        std::println("FreeSync Client help:");
        std::println("File-system specific options:\n"
                     "    --ip=<s>          Ip of the FreeSync server");
        abort();
      } else if (options.ip == NULL) {
        std::println("Ip not set");
        abort();
      }

      this->_client = std::make_shared<FSClient>(options.ip);
      if (!this->_client.get()) {
        std::println("Unable to initiate client");
        abort();
      }
    }

    fuse_main_fn(args.argc, args.argv, &fOps, this->_client.get());
    fuse_opt_free_args(&args);
  }

private:
  std::shared_ptr<FSClient> _client;
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
