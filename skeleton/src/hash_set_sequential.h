#ifndef HASH_SET_SEQUENTIAL_H
#define HASH_SET_SEQUENTIAL_H

#include <functional>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetSequential : public HashSetBase<T> {
 public:
  explicit HashSetSequential(size_t capacity) : table_(capacity) {}

  bool Add(T elem) final {
    bool result = false;
    size_t bucket_idx = std::hash<T>()(elem) % table_.size();

    // Attempts to insert into bucket checking for duplicates
    result = FindOrPushBack(table_[bucket_idx], elem);

    result ? size_++ : size_;

    if (policy()) {
      resize();
    }

    return result;
  }

  bool Remove(T elem) final {
    bool result = false;
    size_t bucket_idx = std::hash<T>()(elem) % table_.size();

    // Attempts to remove from bucket checking for duplicates
    result = FindAndErase(table_[bucket_idx], elem);

    result ? size_-- : size_;

    return result;
  }

  [[nodiscard]] bool Contains(T elem) final {
    size_t bucket_idx = std::hash<T>()(elem) % table_.size();
    std::vector<T>& list = table_[bucket_idx];

    return std::find(list.begin(), list.end(), elem) != list.end();
  }

  [[nodiscard]] size_t Size() const final { return size_; }

 private:
  std::vector<std::vector<T>> table_;
  size_t size_ = 0;

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
    std::vector<std::vector<T>> old_table = table_;
    size_t const new_capacity = old_table.size() * 2;
    table_ = std::vector<std::vector<T>>(new_capacity);

    // reinserting all elems from scratch due to capacity change
    for (std::vector<T>& bucket : old_table) {
      for (T& elem : bucket) {
        table_[std::hash<T>()(elem) % table_.size()].push_back(elem);
      }
    }
  }

  bool policy() { return size_ / table_.size() > 4; }
};

#endif  // HASH_SET_SEQUENTIAL_H
