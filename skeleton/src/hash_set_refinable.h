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
  explicit HashSetRefinable(size_t initial_capacity)
      : capacity_{initial_capacity} {
    for (size_t i = 0; i < capacity_; i++) {
      table_.push_back(std::vector<T>());
      mutexes_.push_back(std::make_unique<std::mutex>());
    }
  }

  bool Add(T elem) final {
    std::unique_lock<std::mutex> resize_lock(resize_mutex_);
    if (ResizePolicy()) {
      std::unique_lock<std::mutex> modification_lock(modification_mutex_);
      resize_cv_.wait(modification_lock,
                      [this] { return modifiers_.load() == 0; });

      Resize();
    }
    modifiers_.fetch_add(1);
    resize_lock.unlock();

    size_t bucket = std::hash<T>()(elem) % capacity_;
    bool found = false;

    std::unique_lock<std::mutex> lock(*mutexes_[bucket]);
    found = std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
            table_[bucket].end();

    if (!found) {
      table_[bucket].push_back(elem);
      set_size_.fetch_add(1);
    }
    lock.unlock();

    if (modifiers_.fetch_sub(1) == 1) {
      resize_cv_.notify_all();
    }

    return !found;
  }

  bool Remove(T elem) final {
    std::unique_lock<std::mutex> resize_lock(resize_mutex_);
    modifiers_.fetch_add(1);
    resize_lock.unlock();

    size_t bucket = std::hash<T>()(elem) % capacity_;
    bool found = false;

    std::unique_lock<std::mutex> lock(*mutexes_[bucket]);
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
      resize_cv_.notify_all();
    }

    return found;
  }

  [[nodiscard]] bool Contains(T elem) final {
    std::unique_lock<std::mutex> resize_lock(resize_mutex_);
    modifiers_.fetch_add(1);
    resize_lock.unlock();

    size_t bucket = std::hash<T>()(elem) % capacity_;
    bool found = false;

    std::unique_lock<std::mutex> lock(*mutexes_[bucket]);
    found = std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
            table_[bucket].end();
    lock.unlock();

    if (modifiers_.fetch_sub(1) == 1) {
      resize_cv_.notify_all();
    }

    return found;
  }

  [[nodiscard]] size_t Size() const final { return set_size_.load(); }

 private:
  std::vector<std::vector<T>> table_;
  std::vector<std::unique_ptr<std::mutex>> mutexes_;

  std::atomic<size_t> set_size_{0};

  size_t capacity_;

  mutable std::mutex resize_mutex_;
  mutable std::condition_variable resize_cv_;

  mutable std::mutex modification_mutex_;
  std::atomic<size_t> modifiers_{0};

  bool ResizePolicy() const { return set_size_.load() * 4 >= capacity_; }

  void Resize() {
    size_t new_capacity = capacity_ * 2;
    table_.resize(new_capacity);
    mutexes_.resize(new_capacity);

    for (size_t i = capacity_; i < new_capacity; i++) {
      mutexes_[i] = std::make_unique<std::mutex>();
    }

    std::vector<T> buffer;
    for (size_t i = 0; i < capacity_; i++) {
      buffer.clear();
      buffer.reserve(table_[i].size());

      for (const auto& item : table_[i]) {
        size_t bucket = std::hash<T>()(item) % new_capacity;
        if (bucket == i) {
          buffer.push_back(item);
        } else {
          table_[bucket].push_back(item);
        }
      }

      if (!buffer.empty()) {
        table_[i] = std::move(buffer);
      } else {
        table_[i].clear();
      }
    }

    capacity_ = new_capacity;
  }
};

#endif  // HASH_SET_REFINABLE_H
