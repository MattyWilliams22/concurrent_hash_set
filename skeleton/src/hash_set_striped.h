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
    if (!resizing_.load() && set_size_.load() >= capacity_.load()) {
      Resize();
    }

    size_t capacity_snapshot = capacity_.load();
    size_t bucket = std::hash<T>()(elem) % capacity_snapshot;
    bool found = false;

    std::unique_lock<std::mutex> lock(*mutexes_[bucket % mutexes_.size()]);
    if (capacity_snapshot != capacity_.load()) {
      bucket = std::hash<T>()(elem) % capacity_.load();
    }

    found = std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
            table_[bucket].end();

    if (!found) {
      table_[bucket].push_back(elem);
      set_size_.fetch_add(1);
    }
    lock.unlock();

    return !found;
  }

  bool Remove(T elem) final {
    size_t capacity_snapshot = capacity_.load();
    size_t bucket = std::hash<T>()(elem) % capacity_snapshot;
    bool found = false;

    std::unique_lock<std::mutex> lock(*mutexes_[bucket % mutexes_.size()]);
    if (capacity_snapshot != capacity_.load()) {
      bucket = std::hash<T>()(elem) % capacity_.load();
    }

    found = std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
            table_[bucket].end();

    if (found) {
      table_[bucket].erase(
          std::remove(table_[bucket].begin(), table_[bucket].end(), elem),
          table_[bucket].end());
      set_size_.fetch_sub(1);
    }
    lock.unlock();

    return found;
  }

  [[nodiscard]] bool Contains(T elem) final {
    size_t capacity_snapshot = capacity_.load();
    size_t bucket = std::hash<T>()(elem) % capacity_snapshot;
    bool found = false;

    std::unique_lock<std::mutex> lock(*mutexes_[bucket % mutexes_.size()]);
    if (capacity_snapshot != capacity_.load()) {
      bucket = std::hash<T>()(elem) % capacity_.load();
    }

    found = std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
            table_[bucket].end();
    lock.unlock();

    return found;
  }

  [[nodiscard]] size_t Size() const final {
    std::scoped_lock<std::mutex> lock(resize_mutex_);

    for (size_t i = 0; i < mutexes_.size(); i++) {
      mutexes_[i]->lock();
    }

    size_t size = set_size_.load();

    for (size_t i = 0; i < mutexes_.size(); i++) {
      mutexes_[i]->unlock();
    }

    return size;
  }

 private:
  std::vector<std::vector<T>> table_;
  std::vector<std::unique_ptr<std::mutex>> mutexes_;

  std::atomic<size_t> set_size_{0};

  std::atomic<std::size_t> capacity_;

  std::atomic<bool> resizing_{false};
  mutable std::mutex resize_mutex_;

  void Resize() {
    std::scoped_lock<std::mutex> resize_lock(resize_mutex_);

    if (set_size_.load() < capacity_.load()) {
      return;
    }

    resizing_.store(true);

    for (size_t i = 0; i < mutexes_.size(); i++) {
      mutexes_[i]->lock();
    }

    for (size_t i = 0; i < capacity_.load(); i++) {
      table_.push_back(std::vector<T>());
    }

    size_t new_capacity = capacity_.load() * 2;
    std::vector<T> buffer{};
    for (size_t i = 0; i < capacity_.load(); i++) {
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

    for (size_t i = 0; i < mutexes_.size(); i++) {
      mutexes_[i]->unlock();
    }

    resizing_.store(false);
  }
};

#endif  // HASH_SET_STRIPED_H
