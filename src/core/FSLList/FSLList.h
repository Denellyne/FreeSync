#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <vector>

template <typename T> struct LListNode;
template <typename T> using LListPtr = std::shared_ptr<LListNode<T>>;

template <typename T> struct LListNode {
  constexpr LListNode(const std::vector<T> &data) : _data(std::move(data)) {}
  std::vector<T> _data;
  LListPtr<T> _next = nullptr;
  std::recursive_mutex _mutex;
  std::shared_mutex _readMutex;
  void removeNext() {
    std::scoped_lock lock{this->_mutex, this->_readMutex};
    if (!this->_next)
      return;
    std::scoped_lock guard{this->_next->_mutex, this->_next->_readMutex};
    if (LListPtr<T> tail = this->_next->_next; tail) {
      std::scoped_lock lock{tail->_mutex, tail->_readMutex};
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
  std::recursive_mutex _mutex;
  std::shared_mutex _readMutex;
  uintptr_t addNode(std::vector<T> &data) {
    LListPtr<T> l = std::make_shared<LListNode<T>>(data);
    std::scoped_lock lock{this->_mutex, l->_mutex, l->_readMutex};
    if (LListPtr<T> cur = getLastNode(); !cur) {
      std::unique_lock guard{this->_readMutex};
      this->_list.swap(l);
      this->_last = this->_list;
      return reinterpret_cast<uintptr_t>(this->_list.get());
    } else {
      std::scoped_lock guard{this->_readMutex, cur->_mutex, cur->_readMutex};
      cur->_next.swap(l);
      this->_last = cur->_next;
      return reinterpret_cast<uintptr_t>(cur->_next.get());
    }
  }
  void removeNode(const uintptr_t id) {
    std::scoped_lock head{this->_mutex};
    if (getId(this->_list) == id) {
      std::scoped_lock lock{this->_mutex, this->_readMutex};
      LListPtr<T> next = this->_list->_next;
      if (next) {
        std::scoped_lock lock{next->_mutex, next->_readMutex};
        this->_list.swap(next);
        return;
      }
      if (this->_last == this->_list) {
        this->_list.swap(next);
        this->_last = this->_list;
        return;
      }
      this->_list.swap(next);
      return;

    } else if (auto headOpt = getPrevNode(id); !headOpt.has_value())
      return;
    else {
      LListPtr<T> head = headOpt.value();
      head->removeNext();
      std::scoped_lock lock{this->_mutex, this->_readMutex, head->_mutex,
                            head->_readMutex};
      if (getId(this->_last) == id) {
        if (head->_next) {
          std::scoped_lock lock{head->_next->_mutex, head->_next->_readMutex};
          this->_last = head->_next;
        } else
          this->_last = head;
      }
      return;
    }
  }

  ~FSLList() { this->freeList(this->_list); }

  std::optional<LListPtr<T>> getNode(const uintptr_t id) const {
    LListPtr<T> node = this->_list;
    while (node) {
      std::shared_lock lock{node->_readMutex};
      if (getId(node) == id)
        return node;
      else if (LListPtr<T> next = node->_next; next) {
        std::shared_lock lock{next->_readMutex};
        if (getId(next) == id)
          return next;
      }
      node = node->_next;
    }
    return std::nullopt;
  }

  std::optional<uintptr_t> getId(LListPtr<T> const &node) const {
    if (!node.get())
      return std::nullopt;
    std::shared_lock lock{node->_readMutex};
    return reinterpret_cast<uintptr_t>(node.get());
  }

private:
  LListPtr<T> getLastNode() {
    std::scoped_lock lock{this->_mutex, this->_readMutex};
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

  std::optional<LListPtr<T>> getPrevNode(const uintptr_t id) const {
    LListPtr<T> node = this->_list;
    while (node) {
      LListPtr<T> prev = node;
      std::shared_lock lock(node->_readMutex);
      LListPtr<T> cur = node->_next;
      if (!cur)
        return std::nullopt;
      else {
        std::shared_lock lock{cur->_readMutex};
        if (getId(cur) == id)
          return prev;
      }
      node = cur;
    }
    return std::nullopt;
  }
  void freeList(LListPtr<T> &node) {
    LListPtr<T> ptr = node;
    while (ptr) {
      removeNode(getId(ptr).value());
      ptr = node;
    }
  }
};
