#ifndef HASH_SET_SEQUENTIAL_H
#define HASH_SET_SEQUENTIAL_H

#include <cassert>
#include <functional>

#include "src/hash_set_base.h"

template <typename T>
class HashSetSequential : public HashSetBase<T> {
 public:
  explicit HashSetSequential(size_t initial_capacity)
      : capacity_(initial_capacity) {
    set_size_ = 0;
    for (size_t i = 0; i < capacity_; i++) {
      table_.push_back(std::vector<T>());
    }
  }

  bool Add(T elem) final {
    if (set_size_ == capacity_) {
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

  void Resize() {
    for (size_t i = 0; i < capacity_; i++) {
      table_.push_back(std::vector<T>());
    }

    std::vector<T> buffer{};
    for (size_t i = 0; i < capacity_; i++) {
      for (size_t j = 0; j < table_[i].size(); j++) {
        int bucket = std::hash<T>()(table_[i][j]) % capacity_;
        if (bucket == i) {
          buffer.push_back(table_[i][j]);
        } else {
          table_[bucket].push_back(table_[i][j]);
        }
      }
      buffer.clear();
    }

    capacity_ = capacity_ * 2;
  }
};

#endif  // HASH_SET_SEQUENTIAL_H
