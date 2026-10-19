#pragma once
#include "../../FSLList/FSLList.h"
#include "../FSProtocol.h"
#include <atomic>
#include <netinet/in.h>
#include <print>
#include <string_view>
#include <unistd.h>
class FSManager {
public:
  FSManager(const std::string_view ip = "localhost",
            const int port = FSMANAGER_PORT, const int backlog = 128) noexcept;
  void run(const std::atomic_bool &running) noexcept;
  uint64_t addNewNode(std::vector<Request> &&requests) noexcept {
    // Check if path is valid for all requests
    return this->_list.addNode(std::move(requests));
  }
  [[nodiscard]] bool removeDeadConnection(const uint64_t connection,
                                          const int id = -1) noexcept {
    auto nodeOpt = this->_list.getNode(connection);
    if (!nodeOpt.has_value())
      return false;
    auto node = nodeOpt.value();
    bool remove = false;
    for (const auto &request : node->_data) {
      if (request.complete == false && (request.id == id || request.id == -1)) {
        remove = true;
        break;
      }
    }
    if (remove)
      this->_list.removeNode(connection);
    return true;
  }

private:
  void removeTimeoutConnections() noexcept {
    std::vector<uint64_t> ids;
    for (const auto &node : this->_list) {
      const auto currentTime =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();
      for (const auto &req : node->_data) {
        if (!req.complete && req.id == -1 &&
            req.timestamp + std::chrono::milliseconds(30000).count() >
                currentTime) {
          ids.emplace_back(this->_list.getId(node).value());
          break;
        }
      }
    }
    for (const auto &id : ids)
      this->_list.removeNode(id);
  }
  class Connection final : public FSProtocol {
  public:
    Connection() = delete;
    Connection(const int fd) noexcept : _fd(fd) {
      this->_private = std::make_unique<RSAKey>(PKEY_PATH, true);
      if (!this->_private) {
        std::println("Unable to load private key");
        this->_fd = -1;
      }
    }
    ~Connection() noexcept {
      std::println("Closing connection of sock:{}", this->_fd);
      close(const_cast<int &>(this->_fd));
    }

    enum processStatus {
      ERR,
      OK,
      NODESET,
      FINISHED,
    };
    processStatus process() noexcept;
    bool confirmFile() noexcept;
    virtual void run() noexcept override {}
    constexpr int getFd() const noexcept { return this->_fd; }
    constexpr uint64_t getNode() const noexcept { return this->_node; }
    constexpr void setNodeId(const uint64_t nodeId) noexcept {
      this->_node = nodeId;
    }
    std::optional<std::span<unsigned char>> getFileContents() const noexcept {
      return this->_file.toSpan();
    }
    bool acquireRequest() noexcept;
    std::string_view getFilePath() const noexcept {
      return this->_file.getFilePath();
    }
    DiskFile &&releaseFile() noexcept { return std::move(this->_file); }
    std::string_view getHash() const noexcept { return this->_hash; }

  private:
    // std::string generateRandomString();
    bool handleValidation() noexcept;
    bool handleAES() noexcept;
    bool interpretCommand(const SSLString &command) noexcept;
    bool checkIfAuthorized(const std::vector<unsigned char> &key) noexcept;

    int _fd = -1;
    DiskFile _file;
    std::string _hash = "";
    uint64_t _node = 0;
  };
  FSLList<Request> _list;
  struct sockaddr_in _serverAddr;
  int _serverFD = -1;
  bool _isOk = false;
};
