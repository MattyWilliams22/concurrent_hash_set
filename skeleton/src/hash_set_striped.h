#ifndef HASH_SET_STRIPED_H
#define HASH_SET_STRIPED_H

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetStriped : public HashSetBase<T> {
 public:
  explicit HashSetStriped(size_t initial_capacity) {
    capacity_.store(initial_capacity);
    for (size_t i = 0; i < capacity_.load(); i++) {
      table_.push_back(std::vector<T>());
      mutexes_.push_back(std::make_unique<std::mutex>());
    }
  }

  bool Add(T elem) final {
    std::mutex mutex;
    std::unique_lock<std::mutex> resize_lock(mutex);
    resize_cv_.wait(resize_lock, [this] { return !resizing_.load(); });

    if (set_size_.load() >= capacity_.load()) {
      Resize();
    }

    size_t bucket = std::hash<T>()(elem) % capacity_.load();
    bool found = false;

    modifiers_.fetch_add(1);
    {
      std::scoped_lock<std::mutex> lock(*mutexes_[bucket % mutexes_.size()]);
      found = std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
              table_[bucket].end();

      if (!found) {
        table_[bucket].push_back(elem);
        set_size_.fetch_add(1);
      }
    }

    modifiers_.fetch_sub(1);
    modification_cv_.notify_all();
    return !found;
  }

  bool Remove(T elem) final {
    std::mutex mutex;
    std::unique_lock<std::mutex> resize_lock(mutex);
    resize_cv_.wait(resize_lock, [this] { return !resizing_.load(); });

    size_t bucket = std::hash<T>()(elem) % capacity_.load();
    bool found = false;

    modifiers_.fetch_add(1);
    {
      std::scoped_lock<std::mutex> lock(*mutexes_[bucket % mutexes_.size()]);
      found = std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
              table_[bucket].end();

      if (found) {
        table_[bucket].erase(
            std::remove(table_[bucket].begin(), table_[bucket].end(), elem),
            table_[bucket].end());
        set_size_.fetch_sub(1);
      }
    }

    modifiers_.fetch_sub(1);
    modification_cv_.notify_all();
    return found;
  }

  [[nodiscard]] bool Contains(T elem) final {
    std::mutex mutex;
    std::unique_lock<std::mutex> resize_lock(mutex);
    resize_cv_.wait(resize_lock, [this] { return !resizing_.load(); });

    size_t bucket = std::hash<T>()(elem) % capacity_.load();
    bool found = false;

    modifiers_.fetch_add(1);
    {
      std::scoped_lock<std::mutex> lock(*mutexes_[bucket % mutexes_.size()]);
      found = std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
              table_[bucket].end();
    }
    modifiers_.fetch_sub(1);
    modification_cv_.notify_all();

    return found;
  }

  [[nodiscard]] size_t Size() const final {
    std::mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);
    modification_cv_.wait(lock, [this] { return modifiers_.load() == 0; });
    return set_size_.load();
  }

 private:
  std::vector<std::vector<T>> table_;
  std::vector<std::unique_ptr<std::mutex>> mutexes_;

  std::atomic<size_t> set_size_{0};
  mutable std::condition_variable modification_cv_;

  std::atomic<std::size_t> capacity_;
  std::atomic<size_t> modifiers_{0};

  std::atomic<bool> resizing_{false};
  std::condition_variable resize_cv_;
  std::mutex resize_mutex_;

  void Resize() {
    std::scoped_lock<std::mutex> resize_lock(resize_mutex_);

    if (set_size_.load() < capacity_.load()) {
      return;
    }

    resizing_.store(true);

    std::mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);
    modification_cv_.wait(lock, [this] { return modifiers_.load() == 0; });

    for (size_t i = 0; i < capacity_.load(); i++) {
      table_.push_back(std::vector<T>());
    }

    size_t new_capacity = capacity_.load() * 2;
    std::vector<T> buffer{};
    for (size_t i = 0; i < capacity_; i++) {
      for (size_t j = 0; j < table_[i].size(); j++) {
        size_t bucket = std::hash<T>()(table_[i][j]) % new_capacity;
        if (bucket == i) {
          buffer.push_back(table_[i][j]);
        } else {
          table_[bucket].push_back(table_[i][j]);
        }
      }
      table_[i].clear();

      for (size_t j = 0; j < buffer.size(); j++) {
        table_[i].push_back(buffer[j]);
      }
      buffer.clear();
    }

    capacity_.store(new_capacity);

    resizing_.store(false);
    resize_cv_.notify_all();
  }
};

#endif  // HASH_SET_STRIPED_H
