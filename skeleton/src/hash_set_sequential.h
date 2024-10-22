#ifndef HASH_SET_SEQUENTIAL_H
#define HASH_SET_SEQUENTIAL_H

#include <cassert>
#include <functional>

#include "src/hash_set_base.h"

template <typename T>
class HashSetSequential : public HashSetBase<T> {
 public:
  explicit HashSetSequential(size_t initial_capacity) {
    set_size = 0;
    for (size_t i = 0; i < initial_capacity; i++) {
      std::vector<T> v;
      table.push_back(v);
    }
  }

  bool Add(T elem) final {
    bool result = false;
    size_t bucket = std::hash<T>()(elem) % table.size();
    result = std::find(table[bucket].begin(), table[bucket].end(), elem) !=
             table[bucket].end();
    table[bucket].push_back(elem);
    set_size = result ? set_size + 1 : set_size;
    return result;
  }

  bool Remove(T elem) final {
    bool result = false;
    size_t bucket = std::hash<T>()(elem) % table.size();
    result = std::find(table[bucket].begin(), table[bucket].end(), elem) !=
             table[bucket].end();
    table[bucket].erase(
        std::remove(table[bucket].begin(), table[bucket].end(), elem),
        table[bucket].end());
    set_size = result ? set_size - 1 : set_size;
    return result;
  }

  [[nodiscard]] bool Contains(T elem) final {
    size_t bucket = std::hash<T>()(elem) % table.size();
    bool result = std::find(table[bucket].begin(), table[bucket].end(), elem) !=
                  table[bucket].end();
    return result;
  }

  [[nodiscard]] size_t Size() const final { return set_size; }

 private:
  std::vector<std::vector<T> > table;
  size_t set_size;
};

#endif  // HASH_SET_SEQUENTIAL_H
