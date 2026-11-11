#pragma once
#include "common.h"
#include <functional>
#include <mutex>
#include <stop_token>
#include <vector>
namespace dr {
class Library {
public:
    explicit Library(fs::path directory);
    Json load();
    Json snapshot() const;
    Json track(const std::string &id) const;
    Json import_paths(const std::vector<fs::path> &inputs, std::function<void(Json)> progress = {}, std::stop_token stop = {});
    Json update(const Json &patch);
    Json remove(const std::string &id);
    Json lyrics(const std::string &id) const;
    Json attach_lyrics(const std::string &id, const fs::path &file);
    const fs::path &directory() const { return directory_; }
    static Json defaults();
private:
    void save_locked();
    fs::path directory_;
    mutable std::mutex mutex_;
    Json state_;
};
}
