#ifndef HASH_SET_STRIPED_H
#define HASH_SET_STRIPED_H

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <vector>

#include "src/hash_set_base.h"

template <typename T>
class HashSetStriped : public HashSetBase<T> {
 public:
  explicit HashSetStriped(size_t initial_capacity)
      : capacity_{initial_capacity} {
    for (size_t i = 0; i < capacity_.load(); i++) {
      table_.push_back(std::vector<T>());
      mutexes_.push_back(std::make_unique<std::mutex>());
    }
  }

  bool Add(T elem) final {
    size_t hash = std::hash<T>()(elem);

    std::unique_lock<std::mutex> lock(*mutexes_[hash % mutexes_.size()]);
    size_t bucket = hash % capacity_.load();
    bool found = std::find(table_[bucket].begin(), table_[bucket].end(),
                           elem) != table_[bucket].end();

    if (!found) {
      table_[bucket].push_back(elem);
      set_size_.fetch_add(1);
    }
    lock.unlock();

    if (ResizePolicy()) {
      Resize();
    }

    return !found;
  }

  bool Remove(T elem) final {
    size_t hash = std::hash<T>()(elem);

    std::unique_lock<std::mutex> lock(*mutexes_[hash % mutexes_.size()]);
    size_t bucket = hash % capacity_.load();
    bool found = std::find(table_[bucket].begin(), table_[bucket].end(),
                           elem) != table_[bucket].end();

    if (found) {
      table_[bucket].erase(
          std::remove(table_[bucket].begin(), table_[bucket].end(), elem),
          table_[bucket].end());
      set_size_.fetch_sub(1);
    }
    lock.unlock();

    return found;
  }

  [[nodiscard]] bool Contains(T elem) final {
    size_t hash = std::hash<T>()(elem);

    std::unique_lock<std::mutex> lock(*mutexes_[hash % mutexes_.size()]);
    size_t bucket = hash % capacity_.load();
    bool found = std::find(table_[bucket].begin(), table_[bucket].end(),
                           elem) != table_[bucket].end();
    lock.unlock();

    return found;
  }

  [[nodiscard]] size_t Size() const final { return set_size_.load(); }

 private:
  std::vector<std::vector<T>> table_;
  std::vector<std::unique_ptr<std::mutex>> mutexes_;

  std::atomic<size_t> set_size_{0};

  std::atomic<size_t> capacity_;

  bool ResizePolicy() const { return set_size_.load() >= 4 * capacity_.load(); }

  void Resize() {
    for (size_t i = 0; i < mutexes_.size(); i++) {
      mutexes_[i]->lock();
    }

    if (!ResizePolicy()) {
      for (size_t i = 0; i < mutexes_.size(); i++) {
        mutexes_[i]->unlock();
      }
      return;
    }

    for (size_t i = 0; i < capacity_.load(); i++) {
      table_.push_back(std::vector<T>());
    }

    size_t new_capacity = capacity_.load() * 2;
    std::vector<T> buffer{};
    for (size_t i = 0; i < capacity_.load(); i++) {
      for (size_t j = 0; j < table_[i].size(); j++) {
        size_t bucket = std::hash<T>()(table_[i][j]) % new_capacity;
        if (bucket == i) {
          buffer.push_back(table_[i][j]);
        } else {
          table_[bucket].push_back(table_[i][j]);
        }
      }
      table_[i].clear();

      for (size_t j = 0; j < buffer.size(); j++) {
        table_[i].push_back(buffer[j]);
      }
      buffer.clear();
    }

    capacity_.store(new_capacity);

    for (size_t i = 0; i < mutexes_.size(); i++) {
      mutexes_[i]->unlock();
    }
  }
};

#endif  // HASH_SET_STRIPED_H
