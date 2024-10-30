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
  explicit HashSetStriped(size_t capacity)
      : table_(capacity),
        locks_(capacity),
        capacity_(capacity),
        num_of_locks_(capacity) {}


  bool Add(T elem) final {
    bool result = false;
    // Due to us always doubling the capacity if we use num_locks (same as
    // initial capacity) to find lock_idx then only load capacity after the lock
    // if saves us doing check such as: if (curr_capacity != capacity_.load()) {
    // lock.unlock();
    // return this->Add(elem);
    //}
    // after acquiring the lock as while I'm holding the bucket lock it would be
    // impossible for another thread to have resized as they would be blocked.
    size_t lock_idx = std::hash<T>()(elem) % num_of_locks_;

    // This time due to fact there are individual locks for each buck we have no
    // way of knowing in resize which lock is already held, it might not even be
    // a thread holding it that is able to resize first hence to prevent
    // deadlocking we must use a unique lock to release the lock early then in
    // resize we attempt to lock the locks in idx order everytime. Including the
    // lock we previously released.
    std::unique_lock<std::mutex> lock(locks_[lock_idx]);

    size_t current_capacity = capacity_.load();
    size_t bucket_idx = std::hash<T>()(elem) % current_capacity;

    result = FindOrPushBack(table_[bucket_idx], elem);

    // Size is atomic here for same reasons as in course gained (due to size
    // func) but also due to multiple threads adding and removing from hash set
    // at the same time, the atomic prevents write-write races.
    if (result) size_.fetch_add(1);

    //  return if we didn't manage to add no need to resize
    if (!result) {
      return false;
    }

    if (policy(current_capacity)) {
      // unlocking here at before resize
      lock.unlock();
      // we must pass capacity here to avoid a double resize as if we get it in the
      // function after it is called we could have been pre-emted before we are able
      // to get the capacity, then another thread resized and then we resized.
      resize(current_capacity);
      return result;
    }

    // auto unique lock unlock here via RAII
    return result;
  }

  bool Remove(T elem) final {
    bool result = false;
    size_t lock_idx = std::hash<T>()(elem) % num_of_locks_;

    std::unique_lock<std::mutex> lock(locks_[lock_idx]);

    size_t curr_capacity = capacity_.load();
    size_t bucket_idx = std::hash<T>()(elem) % curr_capacity;

    result = FindAndErase(table_[bucket_idx], elem);

    if (result) size_.fetch_sub(1);

    return result;
  }

  [[nodiscard]] bool Contains(T elem) final {
    size_t lock_idx = std::hash<T>()(elem) % num_of_locks_;

    std::unique_lock<std::mutex> lock(locks_[lock_idx]);

    size_t curr_capacity = capacity_.load();
    size_t bucket_idx = std::hash<T>()(elem) % curr_capacity;

    std::vector<T>& list = table_[bucket_idx];
    return std::find(list.begin(), list.end(), elem) != list.end();
  }

  [[nodiscard]] size_t Size() const final { return size_.load(); }

 private:
  std::vector<std::vector<T>> table_;
  std::vector<std::mutex> locks_;
  std::atomic<size_t> size_{0};
  std::atomic<size_t> capacity_;
  size_t const num_of_locks_;

  bool policy(size_t const current_capacity) {
    return size_.load() / current_capacity > 4;
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

  void resize(size_t current_capacity) {
    size_t old_capacity = current_capacity;

    std::vector<std::unique_lock<std::mutex>> unique_locks;
    unique_locks.reserve(locks_.size());

    // attempting to lock all mutex's in our locks list in idx order
    for (auto& lock : locks_) {
      unique_locks.emplace_back(lock);
    }

    // if we attempted a resize block on acquiring the unique locks above then
    // continued but the conditions that caused our resize may no longer be true
    // if we were blocked due to another resize in the locks above rather than a
    // add or remove. We no longer resize and return the RAII takes care of
    // unlocking
    if (old_capacity != capacity_.load()) {
      // for (std::mutex& lock : locks_) {
      //   lock.unlock();
      // }
      return;
    }

    std::vector<std::vector<T>> old_table = table_;
    size_t new_capacity = capacity_.load() * 2;
    table_ = std::vector<std::vector<T>>(new_capacity);

    for (std::vector<T>& bucket : old_table) {
      for (T& elem : bucket) {
        table_[std::hash<T>()(elem) % new_capacity].push_back(elem);
      }
    }

    // Capacity is atmoic to prevent write reads of multiple threads with in the
    // resize function at different points
    capacity_.store(new_capacity);

    // auto release unique locks due to RAII
  }
};

#endif  // HASH_SET_STRIPED_H
