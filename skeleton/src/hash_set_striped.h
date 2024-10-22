#ifndef HASH_SET_STRIPED_H
#define HASH_SET_STRIPED_H

#include <cassert>
#include <functional>
#include <mutex>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetStriped : public HashSetBase<T> {
 public:
  explicit HashSetStriped(size_t initial_capacity) {
    set_size_ = 0;
    for (size_t i = 0; i < initial_capacity; i++) {
      table_.push_back(std::vector<T>());
      mutexes_.push_back(std::mutex());
    }
  }

  bool Add(T elem) final {
    size_t bucket = std::hash<T>()(elem) % table_.size();
    bool found = false;

    {
      std::scoped_lock lock(mutexes_[bucket]);
      found = std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
              table_[bucket].end();

      if (!found) {
        table_[bucket].push_back(elem);
      }
    }

    std::scoped_lock lock(size_mutex_);
    if (!found) {
      set_size_++;
    }
    return !found;
  }

  bool Remove(T elem) final {
    size_t bucket = std::hash<T>()(elem) % table_.size();
    bool found = false;

    {
      std::scoped_lock lock(mutexes_[bucket]);
      found = std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
              table_[bucket].end();

      if (found) {
        table_[bucket].erase(
            std::remove(table_[bucket].begin(), table_[bucket].end(), elem),
            table_[bucket].end());
      }
    }

    std::scoped_lock lock(size_mutex_);
    if (found) {
      set_size_--;
    }
    return found;
  }

  [[nodiscard]] bool Contains(T elem) final {
    size_t bucket = std::hash<T>()(elem) % table_.size();

    std::scoped_lock lock(mutexes_[bucket]);
    return std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
           table_[bucket].end();
  }

  [[nodiscard]] size_t Size() const final {
    std::scoped_lock lock(size_mutex_);
    return set_size_;
  }

 private:
  std::vector<std::vector<T> > table_;
  size_t set_size_;
  std::vector<std::mutex> mutexes_;
  std::mutex size_mutex_;
};

#endif  // HASH_SET_STRIPED_H
