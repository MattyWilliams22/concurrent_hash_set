#ifndef HASH_SET_STRIPED_H
#define HASH_SET_STRIPED_H

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetStriped : public HashSetBase<T> {
 public:
  // Sets up a separate lock for each bucket.
  explicit HashSetStriped(size_t capacity)
      : table_(capacity),
        locks_(capacity),
        capacity_(capacity),
        num_of_locks_(capacity) {}

  bool Add(T elem) final {
    bool result = false;

    // Determine the index of the lock for the current element based on its
    // hash.
    size_t lock_idx = std::hash<T>()(elem) % num_of_locks_;

    // Using unique_lock here allows for more control over the lock, which is
    // necessary because we may need to unlock it temporarily during a resize
    // operation. Unlike scoped_lock, unique_lock allows manual unlocking and
    // relocking, which is ideal for managing the lock lifecycle within this
    // more complex operation.
    std::unique_lock<std::mutex> lock(locks_[lock_idx]);

    // Load the current capacity to ensure we're using a consistent value
    // throughout the operation, even if a resize is triggered.
    size_t current_capacity = capacity_.load();
    size_t bucket_idx = std::hash<T>()(elem) % current_capacity;

    // Attempt to insert the element into the appropriate bucket.
    // If the element is already present, the function will return false.
    result = FindOrPushBack(table_[bucket_idx], elem);

    // If the element was added, atomically increment the size.
    if (result) size_.fetch_add(1);

    // If we failed to add the element, no further work is needed, so return
    // early.
    if (!result) {
      return false;
    }

    // Check if resizing is needed based on the load factor policy.
    // If resizing is required, temporarily unlock here to avoid holding locks
    // during resize.
    if (policy(current_capacity)) {
      // Release the lock before resizing to prevent deadlocks.
      lock.unlock();

      // Calling resize with the current capacity to ensure no redundant resizes
      // occur.
      resize(current_capacity);

      return result;  // Return the result of the addition operation.
    }

    // The unique_lock automatically unlocks when it goes out of scope (RAII).
    return result;
  }

  bool Remove(T elem) final {
    bool result = false;
    size_t lock_idx = std::hash<T>()(elem) % num_of_locks_;

    // A scoped_lock is used here to ensure exclusive access to the bucket
    // where the element may be stored. scoped_lock is ideal in this case
    // as it locks the mutex immediately upon construction and releases it
    // automatically when it goes out of scope, ensuring thread-safe access
    // for this simple operation.
    std::scoped_lock lock(locks_[lock_idx]);

    // Compute the index of the bucket in the table where the element should
    // reside.
    size_t curr_capacity = capacity_.load();
    size_t bucket_idx = std::hash<T>()(elem) % curr_capacity;

    // Attempt to find and erase the element from the bucket.
    result = FindAndErase(table_[bucket_idx], elem);

    // If the element was successfully removed, decrement the size atomically.
    if (result) size_.fetch_sub(1);

    return result;
  }

  [[nodiscard]] bool Contains(T elem) final {
    size_t lock_idx = std::hash<T>()(elem) % num_of_locks_;

    // Here we use scoped_lock to ensure that the thread has exclusive access
    // to the bucket while checking if the element exists. Because Contains is
    // a read-only operation, we only need the lock for the duration of this
    // simple, non-modifying access, making scoped_lock an efficient choice.
    std::scoped_lock lock(locks_[lock_idx]);

    // Determine the appropriate bucket index for the given element.
    size_t curr_capacity = capacity_.load();
    size_t bucket_idx = std::hash<T>()(elem) % curr_capacity;

    // Check if the element exists in the bucket by searching through it.
    std::vector<T>& list = table_[bucket_idx];
    return std::find(list.begin(), list.end(), elem) != list.end();
  }

  // Returns the current number of elements in the hash set.
  // Uses an atomic load to ensure a thread-safe read.
  [[nodiscard]] size_t Size() const final { return size_.load(); }

 private:
  std::vector<std::vector<T>>
      table_;  // Hash table represented as a vector of buckets.
  std::vector<std::mutex>
      locks_;  // Array of locks for striped locking of individual buckets.
  std::atomic<size_t> size_{
      0};  // Atomic size to handle concurrent updates safely.
  std::atomic<size_t>
      capacity_;  // Atomic capacity to manage concurrent resizing.
  size_t const
      num_of_locks_;  // Fixed number of locks used for striped locking.

  // Checks if resizing is needed based on the current load factor.
  // Uses atomic size access to ensure consistent reads.
  bool policy(size_t const current_capacity) {
    return size_.load() / current_capacity > 4;
  }

  // Searches for an element in the bucket; if not found, adds it.
  // Returns true if the element was added.
  bool FindOrPushBack(std::vector<T>& list, T& elem) {
    if (std::find(list.begin(), list.end(), elem) == list.end()) {
      list.push_back(elem);
      return true;
    }
    return false;
  }

  // Searches for an element in the bucket; if found, removes it.
  // Returns true if the element was removed.
  bool FindAndErase(std::vector<T>& list, T& elem) {
    auto it = std::find(list.begin(), list.end(), elem);
    if (it != list.end()) {
      list.erase(it);
      return true;
    }
    return false;
  }

  // Resizes the hash table by doubling its capacity if the load factor exceeds
  // the threshold. Locks each bucket individually in index order to avoid
  // deadlock during resizing.
  void resize(size_t current_capacity) {
    size_t old_capacity = current_capacity;

    // Acquire unique locks for all buckets in a consistent order to avoid
    // deadlocks.
    std::vector<std::unique_lock<std::mutex>> unique_locks;
    unique_locks.reserve(locks_.size());

    for (auto& lock : locks_) {
      unique_locks.emplace_back(lock);
    }

    // If another thread has already resized, exit without further action.
    if (old_capacity != capacity_.load()) {
      return;
    }

    // Create a new table with double the capacity.
    std::vector<std::vector<T>> old_table = table_;
    size_t new_capacity = capacity_.load() * 2;
    table_ = std::vector<std::vector<T>>(new_capacity);

    // Reinsert each element from the old table into the resized table.
    for (std::vector<T>& bucket : old_table) {
      for (T& elem : bucket) {
        table_[std::hash<T>()(elem) % new_capacity].push_back(elem);
      }
    }

    // Update capacity to reflect the new size.
    capacity_.store(new_capacity);

    // Unique locks automatically released on exit.
  }
};

#endif  // HASH_SET_STRIPED_H
