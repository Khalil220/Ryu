#pragma once

#include <cstddef>
#include <list>
#include <map>
#include <utility>

namespace ryu {

template <typename Key, typename Value>
class LruCache {
public:
    explicit LruCache(size_t capacity) : capacity_(capacity) {}

    const Value* find(const Key& key) {
        const auto it = index_.find(key);
        if (it == index_.end()) {
            return nullptr;
        }
        entries_.splice(entries_.begin(), entries_, it->second);
        return &it->second->second;
    }

    void put(const Key& key, Value value) {
        if (const auto it = index_.find(key); it != index_.end()) {
            it->second->second = std::move(value);
            entries_.splice(entries_.begin(), entries_, it->second);
            return;
        }
        entries_.emplace_front(key, std::move(value));
        index_.emplace(key, entries_.begin());
        while (entries_.size() > capacity_) {
            index_.erase(entries_.back().first);
            entries_.pop_back();
        }
    }

    size_t size() const { return entries_.size(); }

private:
    using Entries = std::list<std::pair<Key, Value>>;

    size_t capacity_;
    Entries entries_;
    std::map<Key, typename Entries::iterator> index_;
};

}
