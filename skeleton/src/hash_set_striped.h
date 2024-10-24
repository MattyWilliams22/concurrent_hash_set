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
  explicit HashSetStriped(size_t initial_capacity)
      : capacity_{initial_capacity} {
    set_size_.store(0);
    modifiers_.store(0);
    resizing_.store(false);
    for (size_t i = 0; i < capacity_; i++) {
      table_.push_back(std::vector<T>());
      mutexes_.push_back(std::make_unique<std::mutex>());
    }
  }

  bool Add(T elem) final {
    std::unique_lock<std::mutex> resize_lock(resizing_mutex_);
    clear_to_modify_.wait(resize_lock, [this] { return !resizing_.load(); });

    if (set_size_.load() >= capacity_ && !resizing_.load()) {
      resizing_.store(true);
      std::unique_lock<std::mutex> lock(size_mutex_);
      clear_to_read_size_.wait(lock, [this] { return modifiers_.load() == 0; });

      Resize();
      resizing_.store(false);
    }

    size_t bucket = std::hash<T>()(elem) % table_.size();
    bool found = false;

    modifiers_.fetch_add(1);
    {
      std::scoped_lock<std::mutex> lock(*mutexes_[bucket % mutexes_.size()]);
      found = std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
              table_[bucket].end();

      if (!found) {
        table_[bucket].push_back(elem);
      }
    }

    if (!found) {
      set_size_.fetch_add(1);
    }

    modifiers_.fetch_sub(1);
    clear_to_read_size_.notify_all();
    return !found;
  }

  bool Remove(T elem) final {
    size_t bucket = std::hash<T>()(elem) % table_.size();
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
      }
    }

    if (found) {
      set_size_.fetch_sub(1);
    }
    modifiers_.fetch_sub(1);
    clear_to_read_size_.notify_all();
    return found;
  }

  [[nodiscard]] bool Contains(T elem) final {
    size_t bucket = std::hash<T>()(elem) % table_.size();

    std::scoped_lock<std::mutex> lock(*mutexes_[bucket % mutexes_.size()]);
    return std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
           table_[bucket].end();
  }

  [[nodiscard]] size_t Size() const final {
    std::unique_lock<std::mutex> lock(size_mutex_);
    clear_to_read_size_.wait(lock, [this] { return modifiers_.load() == 0; });
    return set_size_.load();
  }

 private:
  std::vector<std::vector<T>> table_;
  std::vector<std::unique_ptr<std::mutex>> mutexes_;

  std::atomic<size_t> set_size_;
  mutable std::mutex size_mutex_;
  mutable std::condition_variable clear_to_read_size_;

  std::size_t capacity_;
  std::atomic<bool> resizing_;
  std::mutex resizing_mutex_;
  std::condition_variable clear_to_modify_;

  std::atomic<size_t> modifiers_;

  void Resize() {
    for (size_t i = 0; i < capacity_; i++) {
      table_.push_back(std::vector<T>());
    }

    size_t new_capacity = capacity_ * 2;
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

    capacity_ = new_capacity;
  }
};

#endif  // HASH_SET_STRIPED_H
