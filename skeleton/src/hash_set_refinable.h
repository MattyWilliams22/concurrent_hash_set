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
      // busy wait while resizing this flag allows mutual ex as in expert from
      // textbook
      if (resizing_flag_.load()) {
      }

      // standard procedure (as from striped) for getting current capacity
      // etc... However, we use a shared mutex here to allow many threads to
      // read the locks_ list but prevent it when we are writing in resize
      current_capacity = capacity_.load();
      bucket_idx = std::hash<T>()(elem) % current_capacity;

      // lock_shared to get a read lock to the locks_list
      locks_list_lock_.lock_shared();
      bucket_lock = locks_[bucket_idx].get();
      locks_list_lock_.unlock_shared();

      // lock as usual to add to our specific bucket
      bucket_lock->lock();

      // someone has started resizing or has resized before we managed to
      // acquire the lock hence we loop again and re calculate which bucket and
      // lock If predicate pass it means no one is resizing or has resized while
      // waiting to lock our information is in-date, and we can proceed adding
      // to the bucket
      if (!resizing_flag_.load() && current_capacity == capacity_.load()) {
        break;
      }
      // unlock and try again as explained above ^
      bucket_lock->unlock();
    }
    // Below explained in striped
    result = FindOrPushBack(table_[bucket_idx], elem);

    if (result) size_.fetch_add(1);

    if (!result) {
      bucket_lock->unlock();
      return false;
    }

    if (size_.load() / current_capacity > 4) {
      bucket_lock->unlock();
      resize(current_capacity);
      return true;
    }

    bucket_lock->unlock();
    return true;
  }

  bool Remove(T elem) final {
    bool result = false;
    size_t bucket_idx;
    std::mutex* bucket_lock;

    // Same locking mechanism as add
    while (true) {
      if (resizing_flag_.load()) {
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

    result = FindAndErase(table_[bucket_idx], elem);

    if (result) size_.fetch_sub(1);

    bucket_lock->unlock();
    return result;
  }

  [[nodiscard]] bool Contains(T elem) final {
    bool result = false;
    size_t bucket_idx;
    std::mutex* bucket_lock;
    size_t current_capacity;

    // Same locking mechanism as add
    while (true) {
      if (resizing_flag_.load()) {
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
  std::atomic<bool> resizing_flag_;
  std::shared_mutex locks_list_lock_;

  void resize(size_t old_capacity) {
    bool expected = false;
    bool desired = true;
    bool result = resizing_flag_.std::atomic<bool>::compare_exchange_strong(
        expected, desired);

    // Someone is already resizing so unnecessary
    if (result != true) {
      return;
    }
    // checks on old capacity captured when resize is called and returns if that
    // capacity is stale, and a thread beat us to resizing
    if (old_capacity != capacity_.load()) {
      resizing_flag_.compare_exchange_strong(desired, expected);
      return;
    }

    // use lock shared than lock_shared to get a reader lock to the locks_list
    // then block until the locks are unlocked --> this forces thread to get
    // caught flag is being unforced
    locks_list_lock_.lock_shared();

    // Effectively is_locked? blocking from the book excerpt c++ style
    for (auto& mutex : locks_) {
      mutex->lock();
      mutex->unlock();
    }

    locks_list_lock_.unlock_shared();

    // same as stripped and coarse grained
    std::vector<std::vector<T>> old_table = table_;
    size_t new_capacity = capacity_.load() * 2;
    table_ = std::vector<std::vector<T>>(new_capacity);

    for (std::vector<T>& bucket : old_table) {
      for (T& elem : bucket) {
        table_[std::hash<T>()(elem) % new_capacity].push_back(elem);
      }
    }

    // now calling lock on shared lock to get a write lock and mutex on whole
    // list as we are modifying it
    locks_list_lock_.lock();
    for (size_t i = 0; i < old_capacity; i++) {
      locks_.push_back(std::make_unique<std::mutex>());
    }
    locks_list_lock_.unlock();

    capacity_.store(new_capacity);

    // Effectively a notify-all for the flag which acts like a condition
    resizing_flag_.compare_exchange_strong(desired, expected);
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
