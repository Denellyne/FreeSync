#pragma once
#include "../../FSLList/FSLList.hpp"
#include "../FSProtocol.h"
#include "../FSServer/FSConnection.hpp"
#include <atomic>
#include <netinet/in.h>
#include <print>
#include <string_view>
#include <unistd.h>
class FSManager {
public:
  FSManager() = delete;
  explicit FSManager(const std::string_view ip = "localhost",
                     const int port = FSMANAGER_PORT,
                     const int backlog = 128) noexcept;
  void run(const std::atomic_bool &running) noexcept;
  uint64_t addNewNode(std::vector<Request> &&requests) noexcept {
    return this->_list.addNode(std::move(requests));
  }
  [[nodiscard]] bool removeDeadConnection(const uint64_t connection,
                                          const int id = -1) noexcept {
    auto nodeOpt = this->_list.getNode(connection);
    if (!nodeOpt.has_value())
      return false;
    const auto &node = nodeOpt.value();

    if (std::any_of(node->_data.cbegin(), node->_data.cend(),
                    [id](const Request &req) {
                      return !req.complete && (req.id == id || req.id == -1);
                    }))
      this->_list.removeNode(connection);
    return true;
  }
  operator bool() const noexcept { return !this->_error; }

private:
  class Connection;
  std::optional<std::string> static finalizeDownload(
      const LListPtr<Request> &node,
      std::unique_ptr<FSManager::Connection> &client, const int sd) noexcept;

  std::optional<std::string> static commitAddTransaction(
      const LListPtr<Request> &node) noexcept;

  std::optional<std::string> static commitDeleteTransaction(
      const LListPtr<Request> &node) noexcept;
  std::optional<std::string>
  setNode(std::unique_ptr<FSManager::Connection> &client,
          const int sd) noexcept;

  void processTransactions() noexcept;

  void removeTimeoutConnections() noexcept {
    std::vector<uint64_t> ids;
    for (const auto &it : this->_list) {
      const LListPtr<Request> &node = it;
      const auto currentTime =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();

      if (std::any_of(
              node->_data.cbegin(), node->_data.cend(),
              [&currentTime](const Request &req) {
                return !req.complete && req.id == -1 &&
                       req.timestamp +
                               std::chrono::milliseconds(30000).count() >
                           currentTime;
              }))
        ids.emplace_back(FSLList<Request>::getId(node).value());
    }
    for (const auto &id : ids)
      this->_list.removeNode(id);
  }
  class Connection final : public FSConnection {
  public:
    Connection() = delete;
    explicit Connection(const int fd) noexcept {
      this->_fd = fd;
      this->_private =
          std::make_unique<RSAKey>(getCanonicalPath(PKEY_PATH), true);
      if (!this->_private || !this->_private->isOk()) {
        std::println("Unable to load private key");
        this->_fd = -1;
      }
    }
    ~Connection() noexcept {
      std::println("Closing connection of sock:{}", this->_fd);
      close(this->_fd);
    }

    enum processStatus : uint8_t {
      ERR,
      OK,
      NODESET,
      FINISHED,
    };
    processStatus process() noexcept;
    bool confirmFile() noexcept;
    void run() noexcept override {}
    [[nodiscard]] constexpr int getFd() const noexcept { return this->_fd; }
    [[nodiscard]] constexpr uint64_t getNode() const noexcept {
      return this->_node;
    }
    constexpr void setNodeId(const uint64_t nodeId) noexcept {
      this->_node = nodeId;
    }
    [[nodiscard]] std::optional<std::span<unsigned char>>
    getFileContents() const noexcept {
      return this->_file.toSpan();
    }
    bool acquireRequest() noexcept;
    [[nodiscard]] std::string_view getOriginalPath() const noexcept {
      return this->_originalPath;
    }
    [[nodiscard]] std::string_view getFilePath() const noexcept {
      return this->_file.getFilePath();
    }
    DiskFile &&releaseFile() noexcept { return std::move(this->_file); }
    [[nodiscard]] std::string_view getHash() const noexcept {
      return this->_hash;
    }

    DiskFile _file;
    std::string _hash, _originalPath;
    uint64_t _node = 0;
  };
  FSLList<Request> _list;
  struct sockaddr_in _serverAddr;
  int _serverFD = -1;
  bool _error = false;
};
