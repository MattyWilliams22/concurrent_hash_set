#ifndef HASH_SET_SEQUENTIAL_H
#define HASH_SET_SEQUENTIAL_H

#include <cassert>
#include <functional>

#include "src/hash_set_base.h"

template <typename T>
class HashSetSequential : public HashSetBase<T> {
 public:
  explicit HashSetSequential(size_t initial_capacity) {
    set_size_ = 0;
    for (size_t i = 0; i < initial_capacity; i++) {
      table_.push_back(std::vector<T>());
    }
  }

  bool Add(T elem) final {
    size_t bucket = std::hash<T>()(elem) % table_.size();
    bool found = std::find(table_[bucket].begin(), table_[bucket].end(),
                           elem) != table_[bucket].end();

    if (!found) {
      table_[bucket].push_back(elem);
      set_size_++;
    }

    return !found;
  }

  bool Remove(T elem) final {
    size_t bucket = std::hash<T>()(elem) % table_.size();

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
    size_t bucket = std::hash<T>()(elem) % table_.size();
    return std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
           table_[bucket].end();
  }

  [[nodiscard]] size_t Size() const final { return set_size_; }

 private:
  std::vector<std::vector<T> > table_;
  size_t set_size_;
};

#endif  // HASH_SET_SEQUENTIAL_H
