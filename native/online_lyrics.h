#pragma once
#include "common.h"
#include "http.h"
#include "lyric_providers.h"
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace dr {
// Online lyrics: AMLL TTML DB plus the providers ported from BetterLyrics / Lyricify (QQ Music,
// NetEase, Kugou, LRCLIB, Apple Music). Every request goes through one HttpFetch whose host
// allowlist is exactly the providers' API hosts (and an optional AMLL mirror), so the page never
// reaches the network. Results are cached per song in the data folder.
class OnlineLyrics {
public:
    struct Options {
        std::set<std::string> providers{"amll", "qq", "netease", "kugou", "lrclib", "apple"};
        std::string amll_base; // empty: the GitHub raw default below
        std::string apple_token;
    };
    static constexpr const char *kAmllDefaultBase = "https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main";
    static const std::vector<std::string> &provider_ids();

    OnlineLyrics(fs::path cache_dir, HttpFetch fetch, std::function<std::string(const std::string &)> fold);
    void set_options(Options options);
    /** Hosts the HTTP client may contact under the current options. */
    bool allowed_host(const std::string &host) const;
    /**
     * Lyrics for a library track from every enabled provider, best first. Cached per track until its
     * metadata or the provider set changes; empty answers are re-checked after three days.
     */
    std::vector<LyricResult> search(const Json &track, bool refresh = false);

    // AMLL TTML DB (exposed for tests).
    struct Match { std::string file, title, artists, album; int score = 0; };
    std::optional<Match> find(const Json &track);
    std::string fetch(const std::string &file);
    static bool valid_file(const std::string &file);
    static bool valid_base(const std::string &url);
    const LyricMatcher &matcher() const { return matcher_; }
private:
    struct Entry { std::string file; std::vector<std::string> titles, artists, albums; };
    void ensure_index();
    std::string amll_get(const std::string &relative, size_t limit);
    std::string amll_base() const;
    fs::path cache_;
    HttpFetch fetch_;
    LyricMatcher matcher_;
    AppleMusicLyrics apple_;
    mutable std::mutex options_mutex_;
    Options options_;
    std::mutex index_mutex_;
    std::vector<Entry> entries_;
    std::string entries_base_;
    std::chrono::steady_clock::time_point loaded_{};
};
}
