#pragma once
#include "common.h"
#include <functional>
#include <mutex>
#include <stop_token>
#include <vector>
#include <unordered_map>
namespace dr {
class Library {
public:
    explicit Library(fs::path directory);
    Json load();
    Json snapshot() const;
    Json settings() const;
    Json track(const std::string &id) const;
    Json import_paths(const std::vector<fs::path> &inputs, std::function<void(Json)> progress = {}, std::stop_token stop = {});
    Json pending_metadata() const;
    Json enrich_metadata(const Json &candidate);
    void flush_metadata();
    Json update(const Json &patch);
    Json remove(const std::string &id);
    Json remove_folder(const std::string &folder);
    Json lyrics(const std::string &id) const;
    Json attach_lyrics(const std::string &id, const fs::path &file);
    const fs::path &directory() const { return directory_; }
    static Json defaults();
private:
    void save_locked();
    void rebuild_index_locked();
    fs::path directory_;
    mutable std::mutex mutex_;
    Json state_;
    std::unordered_map<std::string, size_t> track_index_;
    unsigned long long metadata_revision_ = 0;
    bool metadata_dirty_ = false;
};
}
