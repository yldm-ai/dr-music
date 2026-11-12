#pragma once
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
namespace dr {
// Values already handed to a stream remain alive after eviction. Limits apply
// to cache-owned values; active requests retain only the data they are using.
template<class T> class BoundedCache {
public:
    BoundedCache(size_t max_bytes, size_t max_entries) : max_bytes_(max_bytes), max_entries_(max_entries) {}
    std::shared_ptr<const T> get(const std::string &key) const {
        std::lock_guard lock(mutex_);
        const auto found = entries_.find(key);
        if (found == entries_.end()) return {};
        recent_.splice(recent_.begin(), recent_, found->second.position);
        return found->second.value;
    }
    void put(const std::string &key, std::shared_ptr<const T> value, size_t bytes) {
        std::lock_guard lock(mutex_);
        erase_locked(key);
        if (!value || bytes > max_bytes_ || !max_entries_) return;
        while (!recent_.empty() && (bytes_ > max_bytes_ - bytes || entries_.size() >= max_entries_)) erase_locked(recent_.back());
        recent_.push_front(key); entries_.emplace(key, Entry{std::move(value), bytes, recent_.begin()}); bytes_ += bytes;
    }
    void erase(const std::string &key) { std::lock_guard lock(mutex_); erase_locked(key); }
    void clear() { std::lock_guard lock(mutex_); entries_.clear(); recent_.clear(); bytes_ = 0; }
    size_t bytes() const { std::lock_guard lock(mutex_); return bytes_; }
private:
    void erase_locked(const std::string &key) {
        const auto found = entries_.find(key);
        if (found == entries_.end()) return;
        bytes_ -= found->second.bytes; recent_.erase(found->second.position); entries_.erase(found);
    }
    struct Entry { std::shared_ptr<const T> value; size_t bytes; std::list<std::string>::iterator position; };
    size_t max_bytes_, max_entries_, bytes_ = 0;
    mutable std::mutex mutex_;
    mutable std::list<std::string> recent_;
    std::unordered_map<std::string, Entry> entries_;
};
}
