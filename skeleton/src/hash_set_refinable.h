#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetRefinable : public HashSetBase<T> {
 public:
  explicit HashSetRefinable(size_t initial_capacity) {
    capacity_.store(initial_capacity);
    for (size_t i = 0; i < capacity_.load(); i++) {
      table_.push_back(std::vector<T>());
      mutexes_.push_back(std::make_unique<std::mutex>());
    }
  }

  bool Add(T elem) final {
    std::unique_lock<std::mutex> resize_lock(resize_mutex_);
    resize_cv_.wait(resize_lock);

    modifiers_.fetch_add(1);
    if (!resizing_.load() && set_size_.load() >= capacity_.load()) {
      modifiers_.fetch_sub(1);
      Resize();
      modifiers_.fetch_add(1);
    }

    size_t capacity_snapshot = capacity_.load();
    size_t bucket = std::hash<T>()(elem) % capacity_snapshot;
    bool found = false;

    std::unique_lock<std::mutex> lock(*mutexes_[bucket]);
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
    if (modifiers_.fetch_sub(1) == 1) {
      modification_cv_.notify_all();
    }

    return !found;
  }

  bool Remove(T elem) final {
    std::unique_lock<std::mutex> resize_lock(resize_mutex_);
    resize_cv_.wait(resize_lock);

    modifiers_.fetch_add(1);
    size_t capacity_snapshot = capacity_.load();
    size_t bucket = std::hash<T>()(elem) % capacity_snapshot;
    bool found = false;

    std::unique_lock<std::mutex> lock(*mutexes_[bucket]);
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
    if (modifiers_.fetch_sub(1) == 1) {
      modification_cv_.notify_all();
    }

    return found;
  }

  [[nodiscard]] bool Contains(T elem) final {
    std::unique_lock<std::mutex> resize_lock(resize_mutex_);
    resize_cv_.wait(resize_lock);

    size_t capacity_snapshot = capacity_.load();
    size_t bucket = std::hash<T>()(elem) % capacity_snapshot;
    bool found = false;

    std::unique_lock<std::mutex> lock(*mutexes_[bucket]);
    if (capacity_snapshot != capacity_.load()) {
      bucket = std::hash<T>()(elem) % capacity_.load();
    }

    found = std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
            table_[bucket].end();
    lock.unlock();

    return found;
  }

  [[nodiscard]] size_t Size() const final {
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
  std::condition_variable resize_cv_;

  std::atomic<std::size_t> modifiers_;
  std::condition_variable modification_cv_;
  std::mutex modification_mutex_;

  void Resize() {
    std::unique_lock<std::mutex> resize_lock(resize_mutex_);

    if (set_size_.load() < capacity_.load()) {
      return;
    }

    std::unique_lock<std::mutex> lock(modification_mutex_);
    modification_cv_.wait(lock, [this] { return modifiers_.load() == 0; });

    resizing_.store(true);

    for (size_t i = 0; i < capacity_.load(); i++) {
      table_.push_back(std::vector<T>());
      mutexes_.push_back(std::make_unique<std::mutex>());
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

    resizing_.store(false);
    resize_lock.unlock();
    resize_cv_.notify_all();
  }
};

#endif  // HASH_SET_REFINABLE_H
