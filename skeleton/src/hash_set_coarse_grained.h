#ifndef HASH_SET_COARSE_GRAINED_H
#define HASH_SET_COARSE_GRAINED_H

#include <algorithm>
#include <functional>
#include <mutex>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetCoarseGrained : public HashSetBase<T> {
 public:
  explicit HashSetCoarseGrained(size_t initial_capacity)
      : capacity_{initial_capacity} {
    set_size_ = 0;
    for (size_t i = 0; i < capacity_; i++) {
      table_.push_back(std::vector<T>());
    }
  }

  bool Add(T elem) final {
    std::scoped_lock<std::mutex> lock(mutex_);
    if (ResizePolicy()) {
      Resize();
    }

    size_t bucket = std::hash<T>()(elem) % capacity_;
    bool found = std::find(table_[bucket].begin(), table_[bucket].end(),
                           elem) != table_[bucket].end();

    if (!found) {
      table_[bucket].push_back(elem);
      set_size_++;
    }

    return !found;
  }

  bool Remove(T elem) final {
    std::scoped_lock<std::mutex> lock(mutex_);

    size_t bucket = std::hash<T>()(elem) % capacity_;

    bool found = std::find(table_[bucket].begin(), table_[bucket].end(),
                           elem) != table_[bucket].end();

    if (found) {
      table_[bucket].erase(
          std::remove(table_[bucket].begin(), table_[bucket].end(), elem),
          table_[bucket].end());
      set_size_--;
    }

    return found;
  }

  [[nodiscard]] bool Contains(T elem) final {
    std::scoped_lock<std::mutex> lock(mutex_);

    size_t bucket = std::hash<T>()(elem) % capacity_;
    return std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
           table_[bucket].end();
  }

  [[nodiscard]] size_t Size() const final {
    std::scoped_lock<std::mutex> lock(mutex_);
    return set_size_;
  }

 private:
  std::vector<std::vector<T> > table_;
  size_t set_size_;
  size_t capacity_;
  mutable std::mutex mutex_;

  bool ResizePolicy() const { return set_size_ >= 4 * capacity_; }

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

#endif  // HASH_SET_COARSE_GRAINED_H
