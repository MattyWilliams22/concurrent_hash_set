#ifndef HASH_SET_REFINABLE_H
#define HASH_SET_REFINABLE_H

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <vector>
#include <thread>

#include "src/hash_set_base.h"

template <typename T>
class HashSetRefinable : public HashSetBase<T> {
 public:
  explicit HashSetRefinable(size_t initial_capacity)
      : capacity_{initial_capacity} {
    for (size_t i = 0; i < capacity_.load(); i++) {
      table_.push_back(std::vector<T>());
      mutexes_.push_back(std::mutex());
    }
  }

  bool Add(T elem) final {
    size_t hash = std::hash<T>()(elem);

    acquire(elem);
    size_t bucket = hash % capacity_.load();
    bool found = std::find(table_[bucket].begin(), table_[bucket].end(),
                           elem) != table_[bucket].end();

    if (!found) {
      table_[bucket].push_back(elem);
      set_size_.fetch_add(1);
    }
    mutexes_[hash % mutexes_.size()].unlock();

    if (ResizePolicy()) {
      Resize();
    }

    return !found;
  }

  bool Remove(T elem) final {
    size_t hash = std::hash<T>()(elem);

    acquire(elem);
    size_t bucket = hash % capacity_.load();
    bool found = std::find(table_[bucket].begin(), table_[bucket].end(),
                           elem) != table_[bucket].end();

    if (found) {
      table_[bucket].erase(
          std::remove(table_[bucket].begin(), table_[bucket].end(), elem),
          table_[bucket].end());
      set_size_.fetch_sub(1);
    }
    mutexes_[hash % mutexes_.size()].unlock();

    return found;
  }

  [[nodiscard]] bool Contains(T elem) final {
    size_t hash = std::hash<T>()(elem);

    acquire(elem);
    size_t bucket = hash % capacity_.load();
    bool found = std::find(table_[bucket].begin(), table_[bucket].end(),
                           elem) != table_[bucket].end();
    mutexes_[hash % mutexes_.size()].unlock();

    return found;
  }

  [[nodiscard]] size_t Size() const final { return set_size_.load(); }

 private:
  std::vector<std::vector<T>> table_;
  std::vector<std::mutex> mutexes_;

  std::atomic<size_t> set_size_{0};

  std::atomic<size_t> capacity_;

  std::mutex reference_mutex_;

  bool resizing_{false};

  std::thread::id owner_{std::thread::id()};

  void acquire(T elem) {
    bool mark;
    std::thread::id me = std::this_thread::get_id();
    std::thread::id who;
    while (true) {
      do {
        reference_mutex_.lock();
        mark = resizing_;
        who = owner_;
        reference_mutex_.unlock();
      } while (mark && who != me);

      std::vector<std::mutex> old_mutexes = mutexes_;
      size_t hash = std::hash<T>()(elem);
      old_mutexes[hash % old_mutexes.size()].lock();

      reference_mutex_.lock();
      mark = resizing_;
      who = owner_;
      reference_mutex_.unlock();

      if ((!mark || who == me) && mutexes_ == old_mutexes) {
        return;
      } else {
        old_mutexes[hash % old_mutexes.size()].unlock();
      }
    }
  }

  bool ResizePolicy() const { return set_size_.load() >= 4 * capacity_.load(); }

  bool CompareAndSet(std::thread::id expected_id, std::thread::id new_id,
                     bool expected_mark, bool new_mark) {
    bool compare_and_set = false;
    reference_mutex_.lock();
    if (resizing_ == expected_mark && owner_ == expected_id) {
      compare_and_set = true;
      resizing_ = new_mark;
      owner_ = new_id;
    }
    reference_mutex_.unlock();
    return compare_and_set;
  }

  void Resize() {
    size_t old_capacity = capacity_.load();
    size_t new_capacity = 2 * old_capacity;
    std::thread::id me = std::this_thread::get_id();

    if (CompareAndSet(std::thread::id(), me, false, true)) {
      if (old_capacity != capacity_.load()) {
        reference_mutex_.lock();
        resizing_ = false;
        owner_ = std::thread::id();
        reference_mutex_.unlock();

        return;
      }

      for (std::mutex& mutex : mutexes_) {
        mutex.lock();
      }

      std::vector<std::vector<T>> old_table = table_;
      table_ = std::vector<std::vector<T>>(new_capacity);

      for (size_t i = 0; i < new_capacity; i++) {
        table_.push_back(std::vector<T>());
      }

      mutexes_ = std::vector<std::mutex>(new_capacity);

      for (size_t i = 0; i < new_capacity; i++) {
        mutexes_.push_back(std::mutex());
      }

      for (size_t i = 0; i < capacity_.load(); i++) {
        for (size_t j = 0; j < old_table[i].size(); j++) {
          size_t bucket = std::hash<T>()(old_table[i][j]) % new_capacity;
          table_[bucket].push_back(old_table[i][j]);
        }
      }

      capacity_.store(new_capacity);

      for (size_t i = 0; i < mutexes_.size(); i++) {
        mutexes_[i].unlock();
      }

      reference_mutex_.lock();
      resizing_ = false;
      owner_ = std::thread::id();
      reference_mutex_.unlock();
    }
  }
};

#endif  // HASH_SET_REFINABLE_H
