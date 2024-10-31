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
    // A scoped_lock is used here to ensure that only one thread can access
    // the entire hash set at a time. Since this is a coarse-grained lock
    // implementation, we lock the entire structure rather than individual
    // buckets. This prevents concurrent modifications to the hash set,
    // ensuring thread-safe access without needing finer-grained locking.
    std::scoped_lock<std::mutex> scoped_lock(mutex_);

    // Calculate the bucket index based on the element's hash value.
    size_t bucket_idx = std::hash<T>()(elem) % table_.size();

    // Attempt to insert the element into the appropriate bucket.
    // If the element already exists in the bucket, it is not added again.
    result = FindOrPushBack(table_[bucket_idx], elem);

    // Update the size atomically if the element was successfully added.
    // This ensures that the size remains accurate even with concurrent calls
    // to Add and Remove, which could otherwise cause data races on size.
    if (result) size_.fetch_add(1);

    // Check if a resize is needed based on the load factor policy.
    // Since scoped_lock holds the global lock, resizing is safe here.
    if (policy()) {
      resize();
    }

    return result;
  }

  bool Remove(T elem) final {
    bool result = false;
    // We use a scoped_lock to provide exclusive access to the entire hash set
    // during this operation. The scoped_lock is well-suited here because
    // it simplifies locking by automatically locking upon creation and
    // releasing when going out of scope, which ensures thread-safe removal.
    std::scoped_lock<std::mutex> scoped_lock(mutex_);

    // Calculate the bucket index based on the element's hash value.
    size_t bucket_idx = std::hash<T>()(elem) % table_.size();

    // Attempt to remove the element from the appropriate bucket.
    result = FindAndErase(table_[bucket_idx], elem);

    // Update the size atomically if the element was successfully removed.
    if (result) size_.fetch_sub(1);

    return result;
  }

  [[nodiscard]] bool Contains(T elem) final {
    // The scoped_lock ensures exclusive access to the entire hash set,
    // preventing any other thread from modifying it while we check if an
    // element is present. This guarantees consistent and thread-safe reads.
    std::scoped_lock<std::mutex> scoped_lock(mutex_);

    // Calculate the bucket index based on the element's hash value.
    size_t bucket_idx = std::hash<T>()(elem) % table_.size();

    // Search for the element within the bucket and return whether it was found.
    std::vector<T>& list = table_[bucket_idx];
    return std::find(list.begin(), list.end(), elem) != list.end();
  }

  // Returns the current number of elements in the hash set.
  // Uses an atomic load to ensure a thread-safe read.
  [[nodiscard]] size_t Size() const final { return size_.load(); }

 private:
  std::vector<std::vector<T>>
      table_;  // Hash table represented as a vector of buckets.
  std::atomic<size_t> size_{
      0};             // Atomic size to allow safe concurrent modifications.
  std::mutex mutex_;  // Mutex for coarse-grained locking of the entire table.

  // Checks if an element is in the bucket; if not, adds it to the bucket.
  // Returns true if the element was added.
  bool FindOrPushBack(std::vector<T>& list, T& elem) {
    if (std::find(list.begin(), list.end(), elem) == list.end()) {
      list.push_back(elem);
      return true;
    }
    return false;
  }

  // Checks if an element is in the bucket; if found, removes it from the
  // bucket. Returns true if the element was removed.
  bool FindAndErase(std::vector<T>& list, T& elem) {
    auto it = std::find(list.begin(), list.end(), elem);
    if (it != list.end()) {
      list.erase(it);
      return true;
    }
    return false;
  }

  // Doubles the capacity of the hash table when the load factor threshold is
  // exceeded. As resize is called with a lock held, no other thread can modify
  // the table while resizing.
  void resize() {
    // Old table is stored temporarily while a new table is created with double
    // capacity.
    std::vector<std::vector<T>> old_table = table_;
    size_t new_capacity = old_table.size() * 2;
    table_ = std::vector<std::vector<T>>(new_capacity);

    // Reinsert each element from the old table into the new table.
    for (std::vector<T>& bucket : old_table) {
      for (T& elem : bucket) {
        table_[std::hash<T>()(elem) % table_.size()].push_back(elem);
      }
    }
  }

  // Checks if resizing is needed based on the load factor threshold.
  // Uses atomic size access to ensure a consistent read across threads.
  bool policy() { return size_.load() / table_.size() > 4; }
};

#endif  // HASH_SET_COARSE_GRAINED_H
