#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <vector>

template <typename T> struct LListNode;
template <typename T> using LListPtr = std::shared_ptr<LListNode<T>>;

template <typename T> struct LListNode {
  constexpr LListNode(std::vector<T> &&data) noexcept
      : _data(std::move(data)) {}
  std::vector<T> _data;
  LListPtr<T> _next = nullptr;
  mutable std::shared_mutex _mutex;
  void removeNext() noexcept {
    std::scoped_lock lock{this->_mutex};
    if (!this->_next)
      return;
    std::scoped_lock guard{this->_next->_mutex};
    if (LListPtr<T> tail = this->_next->_next; tail) {
      std::scoped_lock lock{tail->_mutex};
      this->_next.swap(tail);
      return;
    } else
      this->_next.swap(tail);
  }
};

template <typename T> class FSLList {
public:
  LListPtr<T> _list = nullptr;
  LListPtr<T> _last = nullptr;
  mutable std::shared_mutex _mutex;
  uint64_t addNode(std::vector<T> &&data) noexcept {
    LListPtr<T> l = std::make_shared<LListNode<T>>(std::move(data));
    std::scoped_lock lock{this->_mutex};
    if (LListPtr<T> cur = getLastNode(); !cur) {
      this->_list.swap(l);
      this->_last = this->_list;
      return reinterpret_cast<uint64_t>(this->_list.get());
    } else {
      std::scoped_lock guard{cur->_mutex};
      cur->_next.swap(l);
      this->_last = cur->_next;
      return reinterpret_cast<uint64_t>(cur->_next.get());
    }
  }
  void removeNode(const uint64_t id) noexcept {
    std::unique_lock listLock{this->_mutex};

    LListPtr<T> prev = nullptr;
    LListPtr<T> cur = this->_list;

    while (cur) {
      if (getId(cur).value_or(0) == id) {
        const LListPtr<T> next = cur->_next;

        if (prev) {
          std::unique_lock nodeLock{prev->_mutex};
          prev->_next = next;
        } else
          this->_list = next;

        if (this->_last == cur)
          this->_last = prev;

        return;
      }

      prev = cur;
      cur = cur->_next;
    }
  }

  ~FSLList() noexcept { this->freeList(this->_list); }

  std::optional<LListPtr<T>> getNode(const uint64_t id) const noexcept {
    std::shared_lock listLock{this->_mutex};

    LListPtr<T> node = this->_list;

    while (node) {
      if (getId(node).value_or(0) == id)
        return node;

      node = node->_next;
    }

    return std::nullopt;
  }

  std::optional<uint64_t> getId(LListPtr<T> const &node) const noexcept {
    if (!node.get())
      return std::nullopt;
    return reinterpret_cast<uint64_t>(node.get());
  }

  struct LListIterator {
    LListIterator(LListPtr<T> ptr) noexcept : _ptr(ptr) {}
    LListPtr<T> _ptr = nullptr;
    LListIterator &operator++() noexcept {
      if (!this->_ptr)
        return *this;

      LListPtr<T> next;
      {
        std::shared_lock nodeLock{this->_ptr->_mutex};
        next = this->_ptr->_next;
      }

      this->_ptr = std::move(next);
      return *this;
    }
    LListPtr<T> operator*() const noexcept { return this->_ptr; }

    friend bool operator==(const LListIterator &lhs,
                           const LListIterator &rhs) noexcept {
      return lhs._ptr.get() == rhs._ptr.get();
    }
    friend bool operator!=(const LListIterator &lhs,
                           const LListIterator &rhs) noexcept {
      return lhs._ptr.get() != rhs._ptr.get();
    }
  };
  LListIterator begin() noexcept {
    std::shared_lock listLock{this->_mutex};
    return LListIterator{this->_list};
  }

  LListIterator end() noexcept { return LListIterator{nullptr}; }

  LListIterator cbegin() const noexcept {
    std::shared_lock listLock{this->_mutex};
    return LListIterator{this->_list};
  }

  LListIterator cend() const noexcept { return LListIterator{nullptr}; }

private:
  LListPtr<T> getLastNode() noexcept {
    if (this->_last)
      return this->_last;

    return this->_list;

    // LListPtr<T> nodeLoad = node;
    // while (nodeLoad) {
    //   std::shared_lock lock{nodeLoad->_readMutex};
    //   if (!nodeLoad->_next)
    //     return nodeLoad;
    //   else
    //     nodeLoad = nodeLoad->_next;
    // }
    // return nodeLoad;
  }

  std::optional<LListPtr<T>> getPrevNode(const uint64_t id) const noexcept {
    LListPtr<T> node = this->_list;
    while (node) {
      std::shared_lock lock(node->_mutex);
      LListPtr<T> prev = node;
      LListPtr<T> cur = node->_next;
      if (!cur)
        return std::nullopt;
      else {
        std::shared_lock lock{cur->_mutex};
        if (getId(cur) == id)
          return prev;
      }
      node = cur;
    }
    return std::nullopt;
  }
  void freeList(LListPtr<T> &node) noexcept {
    LListPtr<T> ptr = node;
    while (ptr) {
      removeNode(getId(ptr).value());
      ptr = node;
    }
  }
};
