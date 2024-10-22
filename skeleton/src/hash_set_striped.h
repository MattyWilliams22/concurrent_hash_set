#ifndef HASH_SET_STRIPED_H
#define HASH_SET_STRIPED_H

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetStriped : public HashSetBase<T> {
 public:
  explicit HashSetStriped(size_t initial_capacity) {
    set_size_.store(0);
    modifiers_.store(0);
    for (size_t i = 0; i < initial_capacity; i++) {
      table_.push_back(std::vector<T>());
      mutexes_.push_back(std::mutex());
    }
  }

  bool Add(T elem) final {
    size_t bucket = std::hash<T>()(elem) % table_.size();
    bool found = false;

    modifiers_.fetch_add(1);
    {
      std::scoped_lock lock(mutexes_[bucket]);
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
      std::scoped_lock lock(mutexes_[bucket]);
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

    std::scoped_lock lock(mutexes_[bucket]);
    return std::find(table_[bucket].begin(), table_[bucket].end(), elem) !=
           table_[bucket].end();
  }

  [[nodiscard]] size_t Size() const final {
    std::unique_lock lock(size_mutex_);
    clear_to_read_size_.wait(lock, [&] { return modifiers_.load() == 0; });
    return set_size_.load();
  }

 private:
  std::vector<std::vector<T> > table_;
  std::vector<std::mutex> mutexes_;
  std::atomic<size_t> set_size_;
  std::atomic<size_t> modifiers_;
  std::condition_variable clear_to_read_size_;
  std::mutex size_mutex_;
};

#endif  // HASH_SET_STRIPED_H
