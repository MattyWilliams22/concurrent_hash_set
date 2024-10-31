#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetRefinable : public HashSetBase<T> {
 public:
  explicit HashSetRefinable(size_t capacity)
      : table_(capacity), size_(0), capacity_(capacity), resizing_flag_(false) {
    // Sets up a unique mutex for each bucket.
    for (size_t i = 0; i < capacity; i++) {
      locks_.push_back(std::make_unique<std::mutex>());
    }
  }

  bool Add(T elem) final {
    bool result = false;
    size_t current_capacity;
    size_t bucket_idx;
    std::mutex* bucket_lock;

    while (true) {
      // Loop here to retry if resizing is happening; prevents data
      // inconsistency
      if (resizing_flag_.load()) {
        // Busy-wait while another thread is resizing
      }

      // Read current capacity to determine the bucket; may change during resize
      current_capacity = capacity_.load();
      bucket_idx = std::hash<T>()(elem) % current_capacity;

      // Acquire a shared lock on locks_list_lock_ so multiple threads can read
      // the locks_ list while no write access occurs during resize
      locks_list_lock_.lock_shared();
      bucket_lock = locks_[bucket_idx].get();
      locks_list_lock_.unlock_shared();

      // Lock the specific bucket to ensure safe access to it
      bucket_lock->lock();

      // Check again that resizing hasn’t started or completed during our lock
      // acquisition
      if (!resizing_flag_.load() && current_capacity == capacity_.load()) {
        break;  // Exit loop if data is valid and no resize is ongoing
      }
      // Unlock and try again if resizing occurred
      bucket_lock->unlock();
    }

    // Attempt to add the element in the bucket; find or push it into the bucket
    // list
    result = FindOrPushBack(table_[bucket_idx], elem);

    // Atomically increment size if the element was added successfully
    if (result) size_.fetch_add(1);

    if (!result) {
      bucket_lock->unlock();
      return false;
    }

    // Check if resize is needed; if true, release bucket lock and resize
    if (size_.load() / current_capacity > 4) {
      bucket_lock->unlock();
      resize(current_capacity);  // Resize the table
      return true;
    }

    bucket_lock->unlock();  // Unlock the bucket if no resize is needed
    return true;
  }

  bool Remove(T elem) final {
    bool result = false;
    size_t bucket_idx;
    std::mutex* bucket_lock;

    // Similar locking pattern as Add to ensure safety during concurrent access
    // and resizing
    while (true) {
      if (resizing_flag_.load()) {
        // Busy-wait if resizing is ongoing
      }

      size_t current_capacity = capacity_.load();
      bucket_idx = std::hash<T>()(elem) % current_capacity;

      locks_list_lock_.lock_shared();
      bucket_lock = locks_[bucket_idx].get();
      locks_list_lock_.unlock_shared();

      bucket_lock->lock();

      if (!resizing_flag_.load() && current_capacity == capacity_.load()) {
        break;
      }

      bucket_lock->unlock();
    }

    result =
        FindAndErase(table_[bucket_idx], elem);  // Attempt to remove element

    // Atomically decrement size if the element was removed successfully
    if (result) size_.fetch_sub(1);

    bucket_lock->unlock();
    return result;
  }

  [[nodiscard]] bool Contains(T elem) final {
    bool result = false;
    size_t bucket_idx;
    std::mutex* bucket_lock;
    size_t current_capacity;

    // Same locking mechanism as Add to ensure thread-safe reads
    while (true) {
      if (resizing_flag_.load()) {
        // Busy-wait while resizing is happening
      }

      current_capacity = capacity_.load();
      bucket_idx = std::hash<T>()(elem) % current_capacity;

      locks_list_lock_.lock_shared();
      bucket_lock = locks_[bucket_idx].get();
      locks_list_lock_.unlock_shared();

      bucket_lock->lock();

      if (!resizing_flag_.load() && current_capacity == capacity_.load()) {
        break;
      }

      bucket_lock->unlock();
    }

    // Check if the element exists in the bucket
    std::vector<T>& list = table_[bucket_idx];
    result = std::find(list.begin(), list.end(), elem) != list.end();

    bucket_lock->unlock();
    return result;
  }

  [[nodiscard]] size_t Size() const final { return size_.load(); }

 private:
  std::vector<std::vector<T>> table_;
  std::vector<std::unique_ptr<std::mutex>> locks_;
  std::atomic<size_t> size_;
  std::atomic<size_t> capacity_;
  std::atomic<bool> resizing_flag_;  // Flag for signaling ongoing resize
  std::shared_mutex
      locks_list_lock_;  // Shared lock for managing access to locks_ list

  void resize(size_t old_capacity) {
    bool expected = false;
    bool desired = true;
    bool result = resizing_flag_.compare_exchange_strong(expected, desired);

    if (!result) {
      return;  // If another thread is resizing, skip this resize
    }

    if (old_capacity != capacity_.load()) {
      resizing_flag_.compare_exchange_strong(desired, expected);
      return;
    }

    locks_list_lock_.lock_shared();  // Acquire shared lock for reading locks_

    for (auto& mutex : locks_) {
      mutex->lock();
      mutex->unlock();  // Lock and unlock each bucket to ensure no ongoing
                        // operation is blocked
    }

    locks_list_lock_.unlock_shared();

    std::vector<std::vector<T>> old_table = table_;
    size_t new_capacity = capacity_.load() * 2;
    table_ = std::vector<std::vector<T>>(new_capacity);

    for (std::vector<T>& bucket : old_table) {
      for (T& elem : bucket) {
        table_[std::hash<T>()(elem) % new_capacity].push_back(elem);
      }
    }

    // Acquire exclusive lock to modify locks_ list and add new bucket locks
    locks_list_lock_.lock();
    for (size_t i = 0; i < old_capacity; i++) {
      locks_.push_back(std::make_unique<std::mutex>());
    }
    locks_list_lock_.unlock();

    capacity_.store(new_capacity);

    resizing_flag_.compare_exchange_strong(desired,
                                           expected);  // Reset the resize flag
  }

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
};

#endif  // HASH_SET_REFINABLE_H
