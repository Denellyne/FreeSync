#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <print>
#include <queue>
#include <thread>
#include <unistd.h>
#include <vector>

static inline unsigned maxThreads() noexcept {
  return std::thread::hardware_concurrency();
}

typedef std::function<void()> Job;

class ThreadPool {
public:
  ThreadPool() = delete;
  ThreadPool(const unsigned threadCount = 1) {
    std::println("Starting ThreadPool with {} threads", threadCount);
    this->_workers.reserve(threadCount);
    for (unsigned i = 0; i < threadCount; i++) {
      this->_workers.emplace_back(std::thread{[this, i] {
        while (true) {
          Job task;
          {
            std::unique_lock<std::mutex> lock(this->_mutex);

            this->_cv.wait(lock, [this] {
              return !this->_tasks.empty() || this->_terminate;
            });

            if (this->_terminate && this->_tasks.empty()) {
              std::println("Thread {} is shutting down", i);
              return;
            }

            task = std::move(this->_tasks.front());
            this->_tasks.pop();
          }

          try {
            std::println("Thread {} is executing a job", i);
            task();
          } catch (std::exception &e) {
            std::cerr << "Caught exception " << e.what() << '\n';
          }
        }
      }});
    }
  }
  ~ThreadPool() {
    std::println("Shutting down ThreadPool");
    this->shutdown();
  }

  void enqueue(Job task) {
    {
      std::unique_lock<std::mutex> lock(this->_mutex);
      this->_tasks.emplace(std::move(task));
    }
    this->_cv.notify_one();
  }

  void shutdown() {
    this->_terminate.store(true);
    this->_cv.notify_all();

    for (auto &th : this->_workers)
      if (th.joinable())
        th.join();
  }

private:
  std::queue<Job> _tasks;
  std::vector<std::thread> _workers;
  std::condition_variable _cv;
  std::mutex _mutex;
  std::atomic_bool _terminate = false;
};
