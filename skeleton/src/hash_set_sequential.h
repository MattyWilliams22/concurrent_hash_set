#ifndef HASH_SET_SEQUENTIAL_H
#define HASH_SET_SEQUENTIAL_H

#include <algorithm>
#include <functional>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetSequential : public HashSetBase<T> {
 public:
  explicit HashSetSequential(size_t initial_capacity)
      : capacity_{initial_capacity} {
    set_size_ = 0;
    for (size_t i = 0; i < capacity_; i++) {
      table_.push_back(std::vector<T>());
    }
  }

  bool Add(T elem) final {
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
    size_t bucket = std::hash<T>()(elem) % capacity_;
    return std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
           table_[bucket].end();
  }

  [[nodiscard]] size_t Size() const final { return set_size_; }

 private:
  std::vector<std::vector<T> > table_;
  size_t set_size_;
  size_t capacity_;

  bool ResizePolicy() const { return set_size_ >= capacity_; }

  void Resize() {
    size_t new_capacity = capacity_ * 2;
    table_.resize(new_capacity);

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

#endif  // HASH_SET_SEQUENTIAL_H
