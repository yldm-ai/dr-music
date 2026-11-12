#include "online_lyrics.h"
#include <algorithm>
#include <cctype>
#include <future>
#include <sstream>
namespace dr {
namespace {
constexpr auto kIndexMaxAge = std::chrono::hours(24);
constexpr auto kEmptyResultAge = std::chrono::hours(72);
constexpr auto kPartialResultAge = std::chrono::hours(1); // some provider failed (offline, throttled)
constexpr size_t kIndexLimit = 32 * 1024 * 1024, kLyricLimit = 4 * 1024 * 1024;
// Provider API hosts (BetterLyrics / Lyricify endpoints); nothing else is reachable.
const std::set<std::string> kProviderHosts = {
    "raw.githubusercontent.com", "lrclib.net", "lrc.red",
    "music.163.com", "interface.music.163.com", "interface3.music.163.com",
    "u.y.qq.com", "c.y.qq.com",
    "mobilecdn.kugou.com", "lyrics.kugou.com",
    "music.apple.com", "amp-api.music.apple.com",
};
// Word-level formats first when scores tie.
int priority(const std::string &provider) {
    static const std::vector<std::string> order = {"amll", "lrcred", "apple", "qq", "netease", "kugou", "lrclib"};
    return static_cast<int>(std::find(order.begin(), order.end(), provider) - order.begin());
}
Json to_json(const LyricResult &r) {
    return {{"provider", r.provider}, {"title", r.title}, {"artists", r.artists}, {"album", r.album}, {"format", r.format},
            {"text", r.text}, {"reference", r.reference}, {"duration", r.duration}, {"score", r.score}};
}
LyricResult from_json(const Json &j) {
    return {j.value("provider", ""), j.value("title", ""), j.value("artists", ""), j.value("album", ""), j.value("format", ""),
            j.value("text", ""), j.value("reference", ""), j.value("duration", 0.0), j.value("score", 0)};
}
}

const std::vector<std::string> &OnlineLyrics::provider_ids() {
    static const std::vector<std::string> ids = {"amll", "lrcred", "qq", "netease", "kugou", "lrclib", "apple"};
    return ids;
}

OnlineLyrics::OnlineLyrics(fs::path cache_dir, HttpFetch fetch, std::function<std::string(const std::string &)> fold)
    : cache_(std::move(cache_dir)), fetch_(std::move(fetch)), matcher_(std::move(fold)) {}

void OnlineLyrics::set_options(Options options) {
    if (!options.amll_base.empty() && !valid_base(options.amll_base)) options.amll_base.clear();
    while (options.amll_base.ends_with('/')) options.amll_base.pop_back();
    std::lock_guard lock(options_mutex_);
    options_ = std::move(options);
}

std::string OnlineLyrics::amll_base() const {
    std::lock_guard lock(options_mutex_);
    return options_.amll_base.empty() ? kAmllDefaultBase : options_.amll_base;
}

bool OnlineLyrics::allowed_host(const std::string &host) const {
    if (kProviderHosts.contains(host)) return true;
    std::lock_guard lock(options_mutex_);
    const auto mirror = options_.amll_base.empty() ? std::nullopt : parse_url(options_.amll_base);
    return mirror && mirror->host == host;
}

bool OnlineLyrics::valid_base(const std::string &url) {
    // A mirror of the repository root over HTTPS, without query / fragment / credentials.
    const auto parsed = parse_url(url);
    return parsed && parsed->https && url.size() <= 300 && url.find_first_of("?#\"'<> \\") == std::string::npos;
}

bool OnlineLyrics::valid_file(const std::string &file) {
    if (file.size() < 6 || file.size() > 160 || !file.ends_with(".ttml")) return false;
    return std::all_of(file.begin(), file.end() - 5, [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

std::string OnlineLyrics::amll_get(const std::string &relative, size_t limit) {
    auto request = HttpRequest{"GET", amll_base() + "/" + relative, {{"User-Agent", "DR Music 0.2 (+https://github.com/amll-dev/amll-ttml-db)"}}};
    request.limit = limit;
    const auto response = fetch_(request);
    if (response.status == 404) throw std::runtime_error("在线歌词不存在");
    if (response.status != 200) throw std::runtime_error("在线歌词库暂时不可用（HTTP " + std::to_string(response.status) + "）");
    return response.body;
}

void OnlineLyrics::ensure_index() {
    const auto now = std::chrono::steady_clock::now();
    const auto base = amll_base();
    if (!entries_.empty() && entries_base_ == base && now - loaded_ < kIndexMaxAge) return;
    const auto cached = cache_ / L"raw-lyrics-index.jsonl", stamp = cache_ / L"raw-lyrics-index.source";
    std::error_code ec;
    const bool have_cache = fs::is_regular_file(cached, ec);
    const bool same_source = have_cache && fs::is_regular_file(stamp, ec) && read_text(stamp, 1024) == base;
    const bool fresh = same_source && fs::file_time_type::clock::now() - fs::last_write_time(cached, ec) < kIndexMaxAge;
    std::string text;
    if (fresh) text = read_text(cached, kIndexLimit);
    else {
        try {
            text = amll_get("metadata/raw-lyrics-index.jsonl", kIndexLimit);
            fs::create_directories(cache_, ec); write_atomic(cached, text); write_atomic(stamp, base);
        } catch (const std::exception &) {
            if (!have_cache) throw;
            text = read_text(cached, kIndexLimit); // offline: keep matching against the last index
        }
    }
    std::vector<Entry> entries;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        auto json = Json::parse(line, nullptr, false);
        if (!json.is_object() || !json.contains("rawLyricFile") || !json["rawLyricFile"].is_string() || !json.contains("metadata") || !json["metadata"].is_array()) continue;
        Entry e; e.file = json["rawLyricFile"];
        if (!valid_file(e.file)) continue;
        for (const auto &pair : json["metadata"]) {
            if (!pair.is_array() || pair.size() != 2 || !pair[0].is_string() || !pair[1].is_array()) continue;
            const auto name = pair[0].get<std::string>();
            auto *target = name == "musicName" ? &e.titles : name == "artists" ? &e.artists : name == "album" ? &e.albums : nullptr;
            if (target) for (const auto &value : pair[1]) if (value.is_string() && !value.get_ref<const std::string &>().empty()) target->push_back(value);
        }
        if (!e.titles.empty()) entries.push_back(std::move(e));
    }
    if (entries.empty()) throw std::runtime_error("在线歌词索引为空");
    entries_ = std::move(entries); entries_base_ = base; loaded_ = now;
}

std::optional<OnlineLyrics::Match> OnlineLyrics::find(const Json &track) {
    std::lock_guard lock(index_mutex_);
    ensure_index();
    const TrackQuery query{track.value("title", ""), track.value("artist", ""), track.value("album", ""), track.value("duration", 0.0)};
    const Entry *best = nullptr; int best_score = 0;
    for (const auto &e : entries_) {
        // The index has no durations: score on title / artist / album only.
        const int score = matcher_.score({query.title, query.artist, query.album, 0}, {e.titles, e.artists, e.albums, 0});
        if (score && score >= best_score) { best_score = score; best = &e; } // later index lines are newer revisions
    }
    if (!best) return std::nullopt;
    std::string artists;
    for (const auto &a : best->artists) artists += (artists.empty() ? "" : " / ") + a;
    return Match{best->file, best->titles.front(), artists, best->albums.empty() ? "" : best->albums.front(), best_score};
}

std::string OnlineLyrics::fetch(const std::string &file) {
    if (!valid_file(file)) throw std::runtime_error("无效的在线歌词文件");
    const auto cached = cache_ / L"files" / from_text(file);
    std::error_code ec;
    if (fs::is_regular_file(cached, ec)) return read_text(cached, kLyricLimit);
    auto text = amll_get("raw-lyrics/" + file, kLyricLimit);
    if (text.find("<tt") == std::string::npos) throw std::runtime_error("在线歌词格式无效");
    fs::create_directories(cached.parent_path(), ec); write_atomic(cached, text);
    return text;
}

std::vector<LyricResult> OnlineLyrics::search(const Json &track, bool refresh) {
    Options options;
    { std::lock_guard lock(options_mutex_); options = options_; }
    const auto id = track.value("id", "");
    const TrackQuery query{track.value("title", ""), track.value("artist", ""), track.value("album", ""), track.value("duration", 0.0)};
    std::string providers;
    for (const auto &p : provider_ids()) if (options.providers.contains(p) && (p != "apple" || !options.apple_token.empty())) providers += p + ",";
    const auto signature = query.title + "\n" + query.artist + "\n" + query.album + "\n" + std::to_string(static_cast<long long>(query.duration)) + "\n" + providers + "\n" + options.amll_base;
    const bool cacheable = !id.empty() && id.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
    const auto cache_file = cache_ / L"results" / from_text(id + ".json");
    std::error_code ec;
    if (cacheable && !refresh && fs::is_regular_file(cache_file, ec)) {
        auto cached = Json::parse(read_text(cache_file, 64 * 1024 * 1024), nullptr, false);
        if (cached.is_object() && cached.value("signature", "") == signature && cached.contains("results") && cached["results"].is_array()) {
            const auto age = fs::file_time_type::clock::now() - fs::last_write_time(cache_file, ec);
            const bool expired = (cached.value("partial", false) && age > kPartialResultAge) || (cached["results"].empty() && age > kEmptyResultAge);
            if (!expired) {
                std::vector<LyricResult> results;
                for (const auto &r : cached["results"]) results.push_back(from_json(r));
                return results;
            }
        }
    }
    // Providers run concurrently; one failing provider never hides the others.
    std::vector<std::pair<std::string, std::future<std::optional<LyricResult>>>> jobs;
    auto run = [&](const std::string &provider, auto &&body) {
        if (options.providers.contains(provider)) jobs.emplace_back(provider, std::async(std::launch::async, std::forward<decltype(body)>(body)));
    };
    run("amll", [&]() -> std::optional<LyricResult> {
        const auto match = find(track);
        if (!match) return std::nullopt;
        return LyricResult{"amll", match->title, match->artists, match->album, "ttml", fetch(match->file), amll_base() + "/raw-lyrics/" + match->file, 0, match->score};
    });
    run("qq", [&] { return search_qq(fetch_, matcher_, query); });
    run("netease", [&] { return search_netease(fetch_, matcher_, query); });
    run("kugou", [&] { return search_kugou(fetch_, matcher_, query); });
    run("lrclib", [&] { return search_lrclib(fetch_, matcher_, query); });
    run("lrcred", [&] { return search_lrcred(fetch_, matcher_, query); });
    if (!options.apple_token.empty()) run("apple", [&] { return apple_.search(fetch_, matcher_, query, options.apple_token); });
    std::vector<LyricResult> results;
    bool failed = false;
    for (auto &[provider, job] : jobs) {
        try { if (auto result = job.get(); result && !result->text.empty()) results.push_back(std::move(*result)); }
        catch (const std::exception &) { failed = true; } // offline or provider changed: try again next time
    }
    std::stable_sort(results.begin(), results.end(), [](const LyricResult &a, const LyricResult &b) {
        return a.score != b.score ? a.score > b.score : priority(a.provider) < priority(b.provider);
    });
    if (cacheable) {
        Json cached = {{"signature", signature}, {"partial", failed}, {"results", Json::array()}};
        for (const auto &r : results) cached["results"].push_back(to_json(r));
        fs::create_directories(cache_file.parent_path(), ec);
        write_atomic(cache_file, cached.dump(-1, ' ', false, Json::error_handler_t::replace));
    }
    return results;
}
}
