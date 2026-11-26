#pragma once
#include "../../DiskObject/DiskView.hpp"
#include "../../Networking/FSClient/Fuse/FSClientFuse.h"
#include "../../Node/Node.h"
#include <chrono>
#include <expected>
#include <filesystem>
#include <future>
#include <inplace_vector>
#include <optional>
#include <queue>
#include <ranges>
#include <unordered_map>

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
inline static bool addCache(const std::string &path, DiskView *file) noexcept {
  DiskFile hashCache(path + ".hash", true);
  if (!hashCache)
    return false;
  return hashCache.write(Node::hash(file->toSpan()));
}
inline static AuxCode requestFile(const char *path, const std::string &outPath,
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

struct dirEntries {
  size_t count = 0;
  char **entries;
};
inline static void freeDir(dirEntries *map) noexcept {
  for (size_t i = 0; i < map->count; i++)
    free(map->entries[i]);

  delete[] map->entries;

  delete map;
}
