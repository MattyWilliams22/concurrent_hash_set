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

    // Inserts the element into the appropriate bucket if it’s not already
    // present.
    result = FindOrPushBack(table_[bucket_idx], elem);

    // If the element was added successfully, increment the set's size.
    result ? size_++ : size_;

    // Resize the table if it exceeds the load factor threshold.
    if (policy()) {
      resize();
    }

    return result;
  }

  bool Remove(T elem) final {
    bool result = false;
    size_t bucket_idx = std::hash<T>()(elem) % table_.size();

    // Removes the element from the appropriate bucket if it exists.
    result = FindAndErase(table_[bucket_idx], elem);

    // If the element was removed successfully, decrement the set's size.
    result ? size_-- : size_;

    return result;
  }

  [[nodiscard]] bool Contains(T elem) final {
    size_t bucket_idx = std::hash<T>()(elem) % table_.size();
    std::vector<T>& list = table_[bucket_idx];

    // Searches the bucket for the element.
    return std::find(list.begin(), list.end(), elem) != list.end();
  }

  // Returns the current number of elements in the hash set.
  [[nodiscard]] size_t Size() const final { return size_; }

 private:
  std::vector<std::vector<T>>
      table_;        // Hash table represented as a vector of buckets.
  size_t size_ = 0;  // Tracks the number of elements in the set.

  // Searches for an element in a bucket; if not found, adds it to the bucket.
  // Returns true if the element was added.
  bool FindOrPushBack(std::vector<T>& list, T& elem) {
    if (std::find(list.begin(), list.end(), elem) == list.end()) {
      list.push_back(elem);
      return true;
    }
    return false;
  }

  // Searches for an element in a bucket; if found, removes it from the bucket.
  // Returns true if the element was removed.
  bool FindAndErase(std::vector<T>& list, T& elem) {
    auto it = std::find(list.begin(), list.end(), elem);
    if (it != list.end()) {
      list.erase(it);
      return true;
    }
    return false;
  }

  // Resizes the hash table to double its current capacity.
  // Rehashes all elements to distribute them across the new table.
  void resize() {
    std::vector<std::vector<T>> old_table = table_;

    // Allocate a new table with double the previous capacity.
    size_t const new_capacity = old_table.size() * 2;
    table_ = std::vector<std::vector<T>>(new_capacity);

    // Reinsert each element from the old table into the new table.
    for (std::vector<T>& bucket : old_table) {
      for (T& elem : bucket) {
        table_[std::hash<T>()(elem) % table_.size()].push_back(elem);
      }
    }
  }

  // Determines whether the hash table should be resized based on load factor.
  // Returns true if resizing is needed.
  bool policy() { return size_ / table_.size() > 4; }
};

#endif  // HASH_SET_SEQUENTIAL_H
