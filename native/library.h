#pragma once
#include "common.h"
#include "cover.h"
#include <functional>
#include <mutex>
#include <stop_token>
#include <vector>
#include <unordered_map>
namespace dr {
struct CoverResource { std::string bytes, mime; unsigned long long revision = 0; std::string hash; };
class Library {
public:
    explicit Library(fs::path directory);
    Json load();
    Json snapshot() const;
    Json settings() const;
    Json track(const std::string &id) const;
    fs::path audio_path(const std::string &id) const;
    std::shared_ptr<const CoverResource> cover_resource(const std::string &id, const std::string &revision = {}) const;
    // `prune_missing` (rescan): tracks under a fully read input folder whose files are gone are removed,
    // with their favorites / playlist entries. Unreadable folders keep their tracks.
    Json import_paths(const std::vector<fs::path> &inputs, std::function<void(Json)> progress = {}, std::stop_token stop = {}, bool prune_missing = false);
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
    struct CachedLyrics { std::string signature; Json result; };
    CoverCache cover_cache_;
    mutable BoundedCache<CoverResource> cover_bytes_{32 * 1024 * 1024, 512};
    mutable BoundedCache<CachedLyrics> lyric_cache_{8 * 1024 * 1024, 128};
};
}
