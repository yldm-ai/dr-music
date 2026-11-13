#include "lyric_providers.h"
#include "lyric_crypto.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <set>
#include <string_view>
namespace dr {
namespace {
constexpr const char *kBrowserAgent = "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/63.0.3239.132 Safari/537.36";
constexpr const char *kCookie = "os=pc;osver=Microsoft-Windows-10-Professional-build-16299.125-64bit;appver=2.0.3.131777;channel=netease;__remember_me=true";

std::string str(const Json &object, const char *name) {
    if (!object.is_object() || !object.contains(name)) return {};
    const auto &value = object[name];
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return std::to_string(value.get<long long>());
    return {};
}
double number(const Json &object, const char *name) {
    return object.is_object() && object.contains(name) && object[name].is_number() ? object[name].get<double>() : 0.0;
}
const Json &path(const Json &root, std::initializer_list<const char *> keys) {
    static const Json null;
    const Json *node = &root;
    for (auto key : keys) { if (!node->is_object() || !node->contains(key)) return null; node = &(*node)[key]; }
    return *node;
}
Json parse(const HttpResponse &response) {
    if (response.status != 200) return nullptr;
    return Json::parse(response.body, nullptr, false);
}
std::vector<std::string> names(const Json &array) {
    std::vector<std::string> out;
    if (array.is_array()) for (const auto &item : array) if (auto name = str(item, "name"); !name.empty()) out.push_back(name);
    return out;
}
std::wstring without_brackets(const std::wstring &text) {
    std::wstring out; int depth = 0;
    for (auto c : text) {
        if (c == L'(' || c == L'（' || c == L'[' || c == L'【') { ++depth; continue; }
        if ((c == L')' || c == L'）' || c == L']' || c == L'】') && depth) { --depth; continue; }
        if (!depth) out += c;
    }
    return out;
}
bool unknown_artist(const std::string &artist) { return artist.empty() || artist == "未知艺术家"; }
bool unknown_album(const std::string &album) { return album.empty() || album == "未知专辑"; }
std::string xml_unescape(std::string text) {
    std::string out; out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') { out += text[i]; continue; }
        const auto end = text.find(';', i);
        if (end == std::string::npos || end - i > 10) { out += text[i]; continue; }
        const auto entity = text.substr(i + 1, end - i - 1);
        if (entity == "amp") out += '&'; else if (entity == "lt") out += '<'; else if (entity == "gt") out += '>';
        else if (entity == "quot") out += '"'; else if (entity == "apos") out += '\'';
        else if (entity.size() > 1 && entity[0] == '#') {
            const unsigned long code = entity[1] == 'x' || entity[1] == 'X' ? std::stoul(entity.substr(2), nullptr, 16) : std::stoul(entity.substr(1));
            out += utf8(std::wstring(1, static_cast<wchar_t>(code)));
        } else { out += text.substr(i, end - i + 1); }
        i = end;
    }
    return out;
}
// Keeps the best-scoring candidate of a provider's search results.
template<class Item> struct Best { Item item{}; int score = 0; bool found = false; void offer(const Item &value, int s) { if (s > score) { item = value; score = s; found = true; } } };
}

// ---- Matching ---------------------------------------------------------------------------------
LyricMatcher::LyricMatcher(std::function<std::string(const std::string &)> fold) : fold_(std::move(fold)) {}

std::string LyricMatcher::key(const std::string &text) const {
    auto w = wide(fold_ ? fold_(text) : text);
    if (!w.empty()) CharLowerBuffW(w.data(), static_cast<DWORD>(w.size()));
    std::wstring out;
    constexpr std::wstring_view ignored = L" \t\r\n　!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~·・、，。！？：；“”‘’（）【】「」『』《》〈〉～—…♪";
    for (auto c : w) if (ignored.find(c) == std::wstring_view::npos) out += c;
    return utf8(out);
}

std::string LyricMatcher::fingerprint(const std::string &text) const {
    auto w = wide(fold_ ? fold_(text) : text);
    if (!w.empty()) CharLowerBuffW(w.data(), static_cast<DWORD>(w.size()));
    std::vector<std::wstring> words; std::wstring current;
    for (auto c : w) {
        if (iswspace(c) || iswpunct(c) || std::wstring_view(L"·・、，。！？：；“”‘’（）【】「」『』《》〈〉～—…♪　").find(c) != std::wstring_view::npos) { if (!current.empty()) words.push_back(std::move(current)); current.clear(); }
        else current += c;
    }
    if (!current.empty()) words.push_back(std::move(current));
    std::sort(words.begin(), words.end());
    std::wstring out;
    for (const auto &word : words) out += (out.empty() ? L"" : L" ") + word;
    return utf8(out);
}

double LyricMatcher::similarity(const std::string &left, const std::string &right) {
    const auto a = wide(left), b = wide(right);
    if (a.empty() && b.empty()) return 1.0;
    if (a.empty() || b.empty()) return 0.0;
    if (a == b) return 1.0;
    const auto range = std::max<size_t>(std::max(a.size(), b.size()) / 2, 1) - 1;
    std::vector<bool> used_a(a.size()), used_b(b.size());
    size_t matches = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const size_t lo = i > range ? i - range : 0, hi = std::min(b.size(), i + range + 1);
        for (size_t j = lo; j < hi; ++j) if (!used_b[j] && a[i] == b[j]) { used_a[i] = used_b[j] = true; ++matches; break; }
    }
    if (!matches) return 0.0;
    size_t transpositions = 0;
    for (size_t i = 0, j = 0; i < a.size(); ++i) {
        if (!used_a[i]) continue;
        while (!used_b[j]) ++j;
        if (a[i] != b[j]) ++transpositions;
        ++j;
    }
    const double m = static_cast<double>(matches);
    const double jaro = (m / a.size() + m / b.size() + (m - transpositions / 2.0) / m) / 3.0;
    size_t prefix = 0;
    while (prefix < 4 && prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix]) ++prefix;
    return jaro + prefix * 0.1 * (1.0 - jaro);
}

int LyricMatcher::score(const TrackQuery &track, const LyricCandidate &candidate) const {
    constexpr double kTitle = .30, kArtist = .30, kAlbum = .10, kDuration = .30;
    constexpr double kMinTitle = .85, kMinArtist = .70, kThreshold = .75, kFingerprintThreshold = .85;
    if (candidate.titles.empty() || track.title.empty()) return 0;
    const bool durations = track.duration > 0 && candidate.duration > 0;
    const double diff = durations ? std::abs(track.duration - candidate.duration) : 0;
    if (durations && diff > 10) return 0; // another version or another song
    const double duration_score = durations ? (diff <= 1 ? 1.0 : 1.0 - (diff - 1) / 9.0) : 0;
    std::vector<std::string> artists;
    for (const auto &a : candidate.artists) for (auto &part : split_artists(a)) artists.push_back(std::move(part));
    if (unknown_artist(track.artist)) {
        const auto mine = fingerprint(track.title);
        double best = 0;
        for (const auto &t : candidate.titles) {
            if (artists.empty()) best = std::max(best, similarity(mine, fingerprint(t)));
            for (const auto &a : artists) best = std::max(best, similarity(mine, fingerprint(t + " " + a)));
        }
        if (best < kFingerprintThreshold) return 0;
        const double s = durations ? (best * .7 + duration_score * .3) : best;
        return std::max(1, static_cast<int>(s * 100 + .5));
    }
    const auto title_key = key(track.title), base_key = key(utf8(without_brackets(wide(track.title))));
    double title_score = 0;
    for (const auto &t : candidate.titles) {
        title_score = std::max(title_score, similarity(title_key, key(t)));
        if (!base_key.empty() && base_key == key(utf8(without_brackets(wide(t))))) title_score = 1.0;
    }
    double artist_score = 0;
    for (const auto &mine : split_artists(track.artist)) for (const auto &theirs : artists) artist_score = std::max(artist_score, similarity(key(mine), key(theirs)));
    if (title_score < kMinTitle || artist_score < kMinArtist) return 0;
    const bool album = !unknown_album(track.album) && !candidate.albums.empty();
    double album_score = 0;
    if (album) for (const auto &a : candidate.albums) album_score = std::max(album_score, similarity(key(track.album), key(a)));
    const double weight = kTitle + kArtist + (album ? kAlbum : 0) + (durations ? kDuration : 0);
    const double s = (title_score * kTitle + artist_score * kArtist + album_score * (album ? kAlbum : 0) + duration_score * (durations ? kDuration : 0)) / weight;
    if (s < kThreshold) return 0;
    return std::max(1, static_cast<int>(s * 100 + .5));
}

std::vector<std::string> split_artists(const std::string &text) {
    auto w = wide(text);
    for (auto token : {L" feat. ", L" feat ", L" ft. ", L" Feat. ", L" FEAT. ", L" & ", L" x ", L" X ", L" × "})
        for (size_t at; (at = w.find(token)) != std::wstring::npos;) w.replace(at, wcslen(token), L"/");
    std::vector<std::string> parts; std::wstring current;
    auto flush = [&] {
        while (!current.empty() && iswspace(current.back())) current.pop_back();
        size_t start = 0; while (start < current.size() && iswspace(current[start])) ++start;
        if (start < current.size()) parts.push_back(utf8(current.substr(start)));
        current.clear();
    };
    for (auto c : w) { if (std::wstring_view(L"/,&;、，＆／").find(c) != std::wstring_view::npos) flush(); else current += c; }
    flush();
    return parts;
}

std::vector<std::string> search_terms(const TrackQuery &track) {
    auto clean = [](std::string text) {
        for (size_t at; (at = text.find(" - ")) != std::string::npos;) text.replace(at, 3, " ");
        while (!text.empty() && text.back() == ' ') text.pop_back();
        return text;
    };
    auto title = track.title;
    for (auto marker : {"(feat.", " - feat."}) if (auto at = title.find(marker); at != std::string::npos) title = title.substr(0, at);
    while (!title.empty() && title.back() == ' ') title.pop_back();
    std::string artist = unknown_artist(track.artist) ? "" : track.artist;
    for (size_t at; (at = artist.find(", ")) != std::string::npos;) artist.replace(at, 2, " ");
    std::vector<std::string> terms;
    for (auto term : {clean(track.title + " " + artist + " " + (unknown_album(track.album) ? "" : track.album)), clean(title + " " + artist), clean(title)})
        if (!term.empty() && std::find(terms.begin(), terms.end(), term) == terms.end()) terms.push_back(term);
    return terms;
}

// ---- LRCLIB (https://lrclib.net/docs) ------------------------------------------------------------
std::optional<LyricResult> search_lrclib(const HttpFetch &fetch, const LyricMatcher &matcher, const TrackQuery &track) {
    const HttpRequest base{"GET", "", {{"User-Agent", "DR Music 0.2 (local music player; Lyricify/BetterLyrics-style client)"}}};
    std::vector<std::string> urls;
    if (!unknown_artist(track.artist)) urls.push_back("https://lrclib.net/api/search?track_name=" + url_encode(track.title) + "&artist_name=" + url_encode(track.artist));
    urls.push_back("https://lrclib.net/api/search?q=" + url_encode(search_terms(track).back() + (unknown_artist(track.artist) ? "" : " " + track.artist)));
    for (const auto &url : urls) {
        auto request = base; request.url = url;
        const auto json = parse(fetch(request));
        if (!json.is_array() || json.empty()) continue;
        Best<Json> best;
        for (const auto &item : json) {
            if (item.value("instrumental", false)) continue;
            const auto synced = str(item, "syncedLyrics"), plain = str(item, "plainLyrics");
            if (synced.empty() && plain.empty()) continue;
            int s = matcher.score(track, {{str(item, "trackName")}, {str(item, "artistName")}, {str(item, "albumName")}, number(item, "duration")});
            if (s && synced.empty()) s = std::max(1, s - 15); // prefer synced lyrics
            best.offer(item, s);
        }
        if (!best.found) continue;
        const auto &item = best.item;
        const auto synced = str(item, "syncedLyrics");
        return LyricResult{"lrclib", str(item, "trackName"), str(item, "artistName"), str(item, "albumName"), synced.empty() ? "txt" : "lrc",
                           synced.empty() ? str(item, "plainLyrics") : synced, "https://lrclib.net/api/get/" + str(item, "id"), number(item, "duration"), best.score};
    }
    return std::nullopt;
}

// ---- lrc.red (formerly BiniLyrics; https://binimum.org) -----------------------------------------
// The community TTML catalog behind Better Lyrics' "BiniLyrics" source: /search.json?q= → hits keyed
// by ISRC, then /s/<ISRC>.ttml (word or line timed; 404 when the recording has no lyric).
std::optional<LyricResult> search_lrcred(const HttpFetch &fetch, const LyricMatcher &matcher, const TrackQuery &track) {
    const HttpRequest base{"GET", "", {{"User-Agent", "DR Music 0.2 (local music player)"}, {"Accept", "application/json"}}};
    auto valid_isrc = [](const std::string &isrc) {
        return isrc.size() == 12 && std::all_of(isrc.begin(), isrc.end(), [](unsigned char c) { return std::isupper(c) || std::isdigit(c); });
    };
    // "title artist", then the bare title (the catalog searches titles; albums only add noise).
    auto terms = search_terms(track);
    if (terms.size() > 2) terms.erase(terms.begin());
    for (const auto &term : terms) {
        auto request = base; request.url = "https://lrc.red/search.json?q=" + url_encode(term);
        const auto json = parse(fetch(request));
        const auto &hits = path(json, {"hits"});
        if (!hits.is_array() || hits.empty()) continue;
        // Best first; a recording without lyrics answers 404, so fall through to the next candidate.
        std::vector<std::pair<int, const Json *>> ranked;
        for (const auto &hit : hits) {
            if (!valid_isrc(str(hit, "isrc"))) continue;
            if (int s = matcher.score(track, {{str(hit, "title")}, split_artists(str(hit, "artist")), {str(hit, "album")}, number(hit, "duration")}))
                ranked.emplace_back(s, &hit);
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        for (size_t i = 0; i < ranked.size() && i < 3; ++i) {
            const auto &hit = *ranked[i].second;
            const auto url = "https://lrc.red/s/" + str(hit, "isrc") + ".ttml";
            auto lyric = base; lyric.url = url; lyric.headers = {{"User-Agent", "DR Music 0.2 (local music player)"}}; lyric.limit = 4 * 1024 * 1024;
            const auto response = fetch(lyric);
            if (response.status == 404) continue;
            if (response.status != 200) throw std::runtime_error("lrc.red HTTP " + std::to_string(response.status));
            if (response.body.find("<tt") == std::string::npos || response.body.find("<p") == std::string::npos) continue;
            return LyricResult{"lrcred", str(hit, "title"), str(hit, "artist"), str(hit, "album"), "ttml", response.body, url, number(hit, "duration"), ranked[i].first};
        }
        if (!ranked.empty()) return std::nullopt;
    }
    return std::nullopt;
}

// ---- NetEase Cloud Music -------------------------------------------------------------------------
std::string netease_eapi_body(const std::string &api_path, const Json &data) {
    // Lyricify EapiHelper.EApi: md5("nobody{url}use{json}md5forencrypt"), AES-128-ECB with "e82ckenh8dichen8".
    const auto text = data.dump();
    const auto digest = md5_hex("nobody" + api_path + "use" + text + "md5forencrypt");
    const auto message = api_path + "-36cd479b6b5-" + text + "-36cd479b6b5-" + digest;
    return "params=" + hex_upper(aes128_ecb_encrypt(message, "e82ckenh8dichen8"));
}

namespace {
HttpResponse netease_eapi(const HttpFetch &fetch, const std::string &host, const std::string &api_path, Json data) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count(), millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    Json header = {{"__csrf", ""}, {"appver", "8.0.0"}, {"buildver", std::to_string(seconds)}, {"channel", ""}, {"deviceId", ""}, {"mobilename", ""},
                   {"resolution", "1920x1080"}, {"os", "android"}, {"osver", ""}, {"requestId", std::to_string(millis) + "_0000"}, {"versioncode", "140"}, {"MUSIC_U", ""}};
    std::string cookie;
    for (const auto &[k, v] : header.items()) cookie += (cookie.empty() ? "" : "; ") + k + "=" + v.get<std::string>();
    data["header"] = header.dump();
    return fetch({"POST", "https://" + host + "/eapi" + api_path.substr(4),
                  {{"User-Agent", "Mozilla/5.0 (Linux; Android 9; PCT-AL10) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/70.0.3538.64 HuaweiBrowser/10.0.3.311 Mobile Safari/537.36"},
                   {"Referer", "https://music.163.com/"}, {"Cookie", cookie}, {"Content-Type", "application/x-www-form-urlencoded"}},
                  netease_eapi_body(api_path, data)});
}
}

std::optional<LyricResult> search_netease(const HttpFetch &fetch, const LyricMatcher &matcher, const TrackQuery &track) {
    struct Song { std::string id, name, artists, album; double duration = 0; };
    Best<Song> best;
    for (const auto &term : search_terms(track)) {
        Json songs;
        auto web = parse(fetch({"GET", "http://music.163.com/api/search/get/web?csrf_token=hlpretag=&hlposttag=&s=" + url_encode(term) + "&type=1&offset=0&total=true&limit=20",
                                {{"User-Agent", kBrowserAgent}, {"Referer", "https://music.163.com/"}, {"Cookie", kCookie}}}));
        bool eapi = false;
        if (web.is_object() && web.value("code", 0) != -460) songs = path(web, {"result", "songs"});
        if (!songs.is_array() || songs.empty()) { songs = path(parse(netease_eapi(fetch, "interface.music.163.com", "/api/cloudsearch/pc", {{"s", term}, {"type", "1"}, {"limit", "30"}, {"offset", "0"}, {"total", "true"}})), {"result", "songs"}); eapi = true; }
        if (!songs.is_array() || songs.empty()) continue;
        for (const auto &song : songs) {
            const auto artists = names(song.contains(eapi ? "ar" : "artists") ? song[eapi ? "ar" : "artists"] : Json{});
            const auto album = str(path(song, {eapi ? "al" : "album"}), "name");
            const double duration = number(song, eapi ? "dt" : "duration") / 1000.0;
            std::string joined; for (const auto &a : artists) joined += (joined.empty() ? "" : " / ") + a;
            best.offer({str(song, "id"), str(song, "name"), joined, album, duration}, matcher.score(track, {{str(song, "name")}, artists, {album}, duration}));
        }
        if (best.found) break;
    }
    if (!best.found) return std::nullopt;
    const auto lyric = parse(netease_eapi(fetch, "interface3.music.163.com", "/api/song/lyric/v1",
        {{"id", best.item.id}, {"cp", "false"}, {"lv", "0"}, {"kv", "0"}, {"tv", "0"}, {"rv", "0"}, {"yv", "0"}, {"ytv", "0"}, {"yrv", "0"}, {"csrf_token", ""}}));
    auto text = str(path(lyric, {"yrc"}), "lyric");
    std::string format = "yrc";
    if (text.empty()) { text = str(path(lyric, {"lrc"}), "lyric"); format = "lrc"; }
    if (text.empty()) return std::nullopt;
    LyricResult result{"netease", best.item.name, best.item.artists, best.item.album, format, text, "https://music.163.com/song?id=" + best.item.id, best.item.duration, best.score};
    // Romanization written by NetEase's lyric contributors (Japanese / Korean / Cantonese songs), timed like the LRC.
    if (auto roman = str(path(lyric, {"romalrc"}), "lyric"); roman.find('[') != std::string::npos) { result.roman = std::move(roman); result.roman_format = "lrc"; }
    // The translation (mostly Chinese, by contributors), timed like the LRC as well.
    if (auto translation = str(path(lyric, {"tlyric"}), "lyric"); translation.find('[') != std::string::npos) { result.translation = std::move(translation); result.translation_format = "lrc"; }
    return result;
}

// ---- QQ Music ------------------------------------------------------------------------------------
std::string qq_lyric_from_response(const std::string &response, const std::string &tag) {
    // lyric_download.fcg answers XML wrapped in <!-- -->; <content> holds hex QRC (or plain LRC),
    // <contentts> the translation and <contentroma> the romanization, encrypted the same way.
    std::string xml = response;
    for (auto marker : {"<!--", "-->"}) for (size_t at; (at = xml.find(marker)) != std::string::npos;) xml.erase(at, strlen(marker));
    size_t open = std::string::npos;
    for (size_t at = xml.find("<" + tag); at != std::string::npos; at = xml.find("<" + tag, at + 1))
        if (const char next = at + tag.size() + 1 < xml.size() ? xml[at + tag.size() + 1] : '\0'; next == '>' || next == ' ' || next == '/') { open = at; break; } // <content>, not <contentts>
    if (open == std::string::npos) return {};
    const auto start = xml.find('>', open), end = xml.find("</" + tag + ">", open);
    if (start == std::string::npos || end == std::string::npos || end < start) return {};
    auto content = xml.substr(start + 1, end - start - 1);
    if (content.starts_with("<![CDATA[") && content.ends_with("]]>")) content = content.substr(9, content.size() - 12);
    const auto plain = content; // a plain LRC keeps its line breaks and spaces
    content.erase(std::remove_if(content.begin(), content.end(), [](unsigned char c) { return std::isspace(c); }), content.end());
    if (content.empty()) return {};
    std::string text;
    try { text = decrypt_qrc(content); }
    catch (const std::exception &) { return plain.find("[") != std::string::npos ? xml_unescape(plain) : std::string{}; }
    if (text.find("<?xml") == std::string::npos) return text;
    const auto attribute = text.find("LyricContent=\"");
    if (attribute == std::string::npos) return {};
    const auto value_start = attribute + 14, value_end = text.find('"', value_start);
    return value_end == std::string::npos ? std::string{} : xml_unescape(text.substr(value_start, value_end - value_start));
}

std::optional<LyricResult> search_qq(const HttpFetch &fetch, const LyricMatcher &matcher, const TrackQuery &track) {
    struct Song { std::string id, mid, name, artists, album; double duration = 0; };
    // Lyricify sends its NetEase cookie to every provider; QQ does not need it.
    const std::vector<std::pair<std::string, std::string>> headers = {{"User-Agent", kBrowserAgent}, {"Referer", "https://c.y.qq.com/"}};
    Best<Song> best;
    for (const auto &term : search_terms(track)) {
        Json body = {{"req_1", {{"method", "DoSearchForQQMusicDesktop"}, {"module", "music.search.SearchCgiService"},
                                {"param", {{"num_per_page", "20"}, {"page_num", "1"}, {"query", term}, {"search_type", 0}}}}}};
        auto request = HttpRequest{"POST", "https://u.y.qq.com/cgi-bin/musicu.fcg", headers, body.dump()};
        request.headers.emplace_back("Content-Type", "application/json");
        const auto json = parse(fetch(request));
        // req_1.code 2001 is QQ's anti-crawler answer: a failure to retry later, not "no lyrics".
        if (const auto code = path(json, {"req_1", "code"}); code.is_number() && code.get<int>() != 0) throw std::runtime_error("QQ 音乐暂时拒绝了请求（" + std::to_string(code.get<int>()) + "）");
        const auto list = path(json, {"req_1", "data", "body", "song", "list"});
        if (!list.is_array() || list.empty()) continue;
        for (const auto &song : list) {
            const auto artists = names(song.contains("singer") ? song["singer"] : Json{});
            const auto album = str(path(song, {"album"}), "name");
            const auto name = str(song, "name").empty() ? str(song, "title") : str(song, "name");
            std::string joined; for (const auto &a : artists) joined += (joined.empty() ? "" : " / ") + a;
            const double duration = number(song, "interval");
            best.offer({str(song, "id"), str(song, "mid"), name, joined, album, duration}, matcher.score(track, {{name, str(song, "title")}, artists, {album}, duration}));
        }
        if (best.found) break;
    }
    if (!best.found || best.item.id.empty()) return std::nullopt;
    auto request = HttpRequest{"POST", "https://c.y.qq.com/qqmusic/fcgi-bin/lyric_download.fcg", headers, "version=15&miniversion=82&lrctype=4&musicid=" + url_encode(best.item.id)};
    request.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    const auto response = fetch(request);
    if (response.status != 200) return std::nullopt;
    const auto text = qq_lyric_from_response(response.body);
    if (text.empty()) return std::nullopt;
    // QRC lines start "[start,duration]"; LRC lines "[mm:ss.xx]". Metadata lines ([ti:…]) may come first.
    auto is_qrc = [](const std::string &t) {
        if (t.find("](") != std::string::npos) return true;
        for (size_t at = t.find('['); at != std::string::npos; at = t.find('[', at + 1)) {
            size_t i = at + 1; while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) ++i;
            if (i == at + 1 || i >= t.size()) continue;
            if (t[i] == ',') return true;
            if (t[i] == ':') return false;
        }
        return false;
    };
    const bool qrc = is_qrc(text);
    LyricResult result{"qq", best.item.name, best.item.artists, best.item.album, qrc ? "qrc" : "lrc", text, "https://y.qq.com/n/ryqq/songDetail/" + best.item.mid, best.item.duration, best.score};
    // The romanization is a QRC with the same line and word timing as the lyric.
    if (auto roman = qq_lyric_from_response(response.body, "contentroma"); roman.find('[') != std::string::npos) {
        result.roman_format = is_qrc(roman) ? "qrc" : "lrc";
        result.roman = std::move(roman);
    }
    if (auto translation = qq_lyric_from_response(response.body, "contentts"); translation.find('[') != std::string::npos) {
        result.translation_format = is_qrc(translation) ? "qrc" : "lrc";
        result.translation = std::move(translation);
    }
    return result;
}

// ---- Kugou ---------------------------------------------------------------------------------------
std::optional<LyricResult> search_kugou(const HttpFetch &fetch, const LyricMatcher &matcher, const TrackQuery &track) {
    struct Song { std::string hash, name, artists, album; double duration = 0; };
    Best<Song> best;
    for (const auto &term : search_terms(track)) {
        const auto info = path(parse(fetch({"GET", "http://mobilecdn.kugou.com/api/v3/search/song?format=json&keyword=" + url_encode(term) + "&page=1&pagesize=20&showtype=1", {{"User-Agent", kBrowserAgent}}})), {"data", "info"});
        if (!info.is_array() || info.empty()) continue;
        for (const auto &song : info) {
            const auto artists = str(song, "singername");
            const double duration = number(song, "duration");
            best.offer({str(song, "hash"), str(song, "songname"), artists, str(song, "album_name"), duration}, matcher.score(track, {{str(song, "songname")}, {artists}, {str(song, "album_name")}, duration}));
        }
        if (best.found) break;
    }
    if (!best.found || best.item.hash.empty()) return std::nullopt;
    const auto candidates = path(parse(fetch({"GET", "https://lyrics.kugou.com/search?ver=1&man=yes&client=pc&keyword=&hash=" + url_encode(best.item.hash), {{"User-Agent", kBrowserAgent}}})), {"candidates"});
    if (!candidates.is_array() || candidates.empty()) return std::nullopt;
    const auto &candidate = candidates[0];
    const auto download = parse(fetch({"GET", "https://lyrics.kugou.com/download?ver=1&client=pc&id=" + url_encode(str(candidate, "id")) + "&accesskey=" + url_encode(str(candidate, "accesskey")) + "&fmt=krc&charset=utf8", {{"User-Agent", kBrowserAgent}}}));
    const auto content = str(download, "content");
    if (content.empty()) return std::nullopt;
    return LyricResult{"kugou", best.item.name, best.item.artists, best.item.album, "krc", decrypt_krc(content), "https://www.kugou.com/song/#hash=" + best.item.hash, best.item.duration, best.score};
}

// ---- Apple Music ---------------------------------------------------------------------------------
std::string apple_pick_token(const std::string &script, long long now) {
    // BetterLyrics AppleMusic.GetAccessTokenScore: prefer unexpired WebPlayKid / AMPWebPlay tokens.
    std::string best; int best_score = -1;
    auto token_char = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.'; };
    for (size_t at = script.find("eyJ"); at != std::string::npos; at = script.find("eyJ", at + 3)) {
        size_t end = at; while (end < script.size() && token_char(script[end])) ++end;
        const auto token = script.substr(at, end - at);
        const auto first = token.find('.'), second = token.find('.', first + 1);
        if (first == std::string::npos || second == std::string::npos || token.find('.', second + 1) != std::string::npos) continue;
        auto decode = [](std::string part) {
            std::replace(part.begin(), part.end(), '-', '+'); std::replace(part.begin(), part.end(), '_', '/');
            while (part.size() % 4) part += '=';
            try { return Json::parse(base64_decode(part), nullptr, false); } catch (const std::exception &) { return Json{}; }
        };
        const auto header = decode(token.substr(0, first)), payload = decode(token.substr(first + 1, second - first - 1));
        if (!payload.is_object() || !payload.contains("exp") || !payload["exp"].is_number() || payload["exp"].get<long long>() - 60 <= now) continue;
        int score = 0;
        if (_stricmp(str(header, "kid").c_str(), "WebPlayKid") == 0) score += 100;
        if (_stricmp(str(payload, "iss").c_str(), "AMPWebPlay") == 0) score += 100;
        if (payload.contains("root_https_origin")) score += 10;
        if (score > best_score) { best_score = score; best = token; }
    }
    return best;
}

void AppleMusicLyrics::ensure_session(const HttpFetch &fetch, const std::string &media_user_token) {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::vector<std::pair<std::string, std::string>> web = {{"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/110.0.0.0 Safari/537.36"},
                                                                  {"Origin", "https://music.apple.com"}, {"Referer", "https://music.apple.com/"}};
    if (token_.empty() || expires_ - 60 <= now) {
        token_.clear();
        const auto page = fetch({"GET", "https://music.apple.com/us/browse", web});
        if (page.status != 200) throw std::runtime_error("无法访问 Apple Music");
        std::vector<std::string> scripts;
        for (size_t at = page.body.find("assets/index"); at != std::string::npos; at = page.body.find("assets/index", at + 1)) {
            size_t start = at; while (start > 0 && std::string_view("\"'<> \t\r\n=").find(page.body[start - 1]) == std::string_view::npos) --start;
            size_t end = at; while (end < page.body.size() && std::string_view("\"'<> \t\r\n").find(page.body[end]) == std::string_view::npos) ++end;
            auto url = page.body.substr(start, end - start);
            if (!url.ends_with(".js") || url.find("legacy") != std::string::npos) continue;
            if (!url.starts_with("https://")) url = "https://music.apple.com" + std::string(url.starts_with("/") ? "" : "/") + url;
            if (std::find(scripts.begin(), scripts.end(), url) == scripts.end()) scripts.push_back(url);
        }
        for (const auto &url : scripts) {
            auto request = HttpRequest{"GET", url, web}; request.limit = 32 * 1024 * 1024;
            const auto script = fetch(request);
            if (script.status != 200) continue;
            token_ = apple_pick_token(script.body, now);
            if (!token_.empty()) break;
        }
        if (token_.empty()) throw std::runtime_error("无法获取 Apple Music 访问令牌");
        const auto first = token_.find('.'), second = token_.find('.', first + 1);
        auto payload = token_.substr(first + 1, second - first - 1);
        std::replace(payload.begin(), payload.end(), '-', '+'); std::replace(payload.begin(), payload.end(), '_', '/');
        while (payload.size() % 4) payload += '=';
        expires_ = Json::parse(base64_decode(payload)).value("exp", 0LL);
        session_user_.clear();
    }
    if (session_user_ != media_user_token || storefront_.empty()) {
        auto headers = web; headers.emplace_back("Authorization", "Bearer " + token_); headers.emplace_back("media-user-token", media_user_token); headers.emplace_back("Accept", "application/json");
        const auto json = parse(fetch({"GET", "https://amp-api.music.apple.com/v1/me/storefront", headers}));
        const auto &data = path(json, {"data"});
        if (!data.is_array() || data.empty()) throw std::runtime_error("Apple Music 令牌无效或已过期");
        storefront_ = str(data[0], "id"); language_ = str(path(data[0], {"attributes"}), "defaultLanguageTag");
        if (storefront_.empty() || storefront_.find_first_not_of("abcdefghijklmnopqrstuvwxyz") != std::string::npos) throw std::runtime_error("Apple Music 地区无效");
        session_user_ = media_user_token;
    }
}

std::optional<LyricResult> AppleMusicLyrics::search(const HttpFetch &fetch, const LyricMatcher &matcher, const TrackQuery &track, const std::string &media_user_token) {
    if (media_user_token.empty()) return std::nullopt;
    std::lock_guard lock(mutex_);
    ensure_session(fetch, media_user_token);
    std::vector<std::pair<std::string, std::string>> headers = {{"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/110.0.0.0 Safari/537.36"},
        {"Origin", "https://music.apple.com"}, {"Referer", "https://music.apple.com/"}, {"Accept", "application/json"}, {"Authorization", "Bearer " + token_}, {"media-user-token", media_user_token}};
    const auto api = "https://amp-api.music.apple.com/v1/catalog/" + storefront_;
    const auto language = language_.empty() ? std::string{} : "&l=" + url_encode(language_);
    struct Song { std::string id, name, artists, album; double duration = 0; };
    Best<Song> best;
    for (const auto &term : search_terms(track)) {
        const auto songs = path(parse(fetch({"GET", api + "/search?term=" + url_encode(term) + "&types=songs&limit=10" + language, headers})), {"results", "songs", "data"});
        if (!songs.is_array() || songs.empty()) continue;
        for (const auto &song : songs) {
            const auto &a = path(song, {"attributes"});
            const double duration = number(a, "durationInMillis") / 1000.0;
            best.offer({str(song, "id"), str(a, "name"), str(a, "artistName"), str(a, "albumName"), duration}, matcher.score(track, {{str(a, "name")}, {str(a, "artistName")}, {str(a, "albumName")}, duration}));
        }
        if (best.found) break;
    }
    if (!best.found || best.item.id.empty() || best.item.id.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
    const auto song = parse(fetch({"GET", api + "/songs/" + best.item.id + "?include[songs]=lyrics,syllable-lyrics&extend=ttmlLocalizations" + language, headers}));
    const auto &data = path(song, {"data"});
    if (!data.is_array() || data.empty()) return std::nullopt;
    std::string ttml;
    for (auto relation : {"syllable-lyrics", "lyrics"}) {
        const auto &items = path(data[0], {"relationships", relation, "data"});
        if (!items.is_array() || items.empty()) continue;
        ttml = str(path(items[0], {"attributes"}), "ttml");
        if (!ttml.empty() && (std::string_view(relation) == "lyrics" || ttml.find("begin=") != std::string::npos)) break;
        ttml.clear();
    }
    if (ttml.empty()) return std::nullopt;
    return LyricResult{"apple", best.item.name, best.item.artists, best.item.album, "ttml", ttml, "https://music.apple.com/song/" + best.item.id, best.item.duration, best.score};
}
}
