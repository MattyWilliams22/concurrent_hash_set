#ifndef HASH_SET_COARSE_GRAINED_H
#define HASH_SET_COARSE_GRAINED_H

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetCoarseGrained : public HashSetBase<T> {
 public:
  explicit HashSetCoarseGrained(size_t capacity) : table_(capacity) {}

  bool Add(T elem) final {
    bool result = false;
    // Only a single thread can can access entire hashset at a time due to this
    // it is unnecessary to do an early unlock before resize and no thread could
    // be resizing before us once we have the mutex lock. Hence scoped lock is
    // perfect.

    std::scoped_lock<std::mutex> scoped_lock(mtx_);

    size_t bucket_idx = std::hash<T>()(elem) % table_.size();
    result = FindOrPushBack(table_[bucket_idx], elem);

    // Even though one only thread can change size value at once
    // we must make it atomic, this is due to the size which can clash with Add
    // Remove methods hence there will be a write read data race on the size()
    // function if it is not atomic
    if (result) size_.fetch_add(1);

    if (policy()) {
      resize();
    }

    return result;
  }

  bool Remove(T elem) final {
    bool result = false;
    // blocking to access to global mutex over hashset table
    std::scoped_lock<std::mutex> scoped_lock(mtx_);

    size_t bucket_idx = std::hash<T>()(elem) % table_.size();
    result = FindAndErase(table_[bucket_idx], elem);

    if (result) size_.fetch_sub(1);

    return result;
  }

  [[nodiscard]] bool Contains(T elem) final {
    // blocking to access to global mutex over hashset table
    std::scoped_lock<std::mutex> scoped_lock(mtx_);

    size_t bucket_idx = std::hash<T>()(elem) % table_.size();
    std::vector<T>& list = table_[bucket_idx];

    return std::find(list.begin(), list.end(), elem) != list.end();
  }

  [[nodiscard]] size_t Size() const final { return size_.load(); }

 private:
  std::vector<std::vector<T>> table_;
  std::atomic<size_t> size_{0};
  std::mutex mtx_;

  bool FindOrPushBack(std::vector<T>& list, T& elem) {
    if (std::find(list.begin(), list.end(), elem) == list.end()) {
      list.push_back(elem);
      return true;
    }
    return false;
  }

  bool FindAndErase(std::vector<T>& list, T& elem) {
    auto it = std::find(list.begin(), list.end(), elem);
    if (it != list.end()) {
      list.erase(it);
      return true;
    }
    return false;
  }

  void resize() {
    // Enters func still holding lock on entire table_ from add function
    // creating mutual ex.
    std::vector<std::vector<T>> old_table = table_;
    size_t new_capacity = old_table.size() * 2;
    table_ = std::vector<std::vector<T>>(new_capacity);

    for (std::vector<T>& bucket : old_table) {
      for (T& elem : bucket) {
        table_[std::hash<T>()(elem) % table_.size()].push_back(elem);
      }
    }
  }
  // leveraging atomic to prevent read write read.
  bool policy() { return size_.load() / table_.size() > 4; }
};

#endif  // HASH_SET_COARSE_GRAINED_H
