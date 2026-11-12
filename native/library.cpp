#include "library.h"
#include "cover.h"
#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>
#include <tvariant.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <unordered_set>
#include <chrono>
#include <charconv>
namespace dr {
namespace {
const std::set<std::string> audio_extensions{ ".mp3", ".flac", ".wav", ".m4a", ".aac", ".ogg", ".opus", ".webm", ".mp4" };
const std::set<std::string> lyric_extensions{ ".ttml", ".yrc", ".lrc", ".txt" };
std::string string_or(const TagLib::String &text, const std::string &fallback) {
    const auto trimmed = text.stripWhiteSpace();
    return trimmed.isEmpty() ? fallback : trimmed.to8Bit(true);
}
std::string property_text(const TagLib::PropertyMap &properties, const char *key) {
    std::string result;
    for (const auto &value : properties.value(key)) {
        const auto text = string_or(value, "");
        if (!text.empty()) { if (!result.empty()) result += " / "; result += text; }
    }
    return result;
}
unsigned property_number(const TagLib::PropertyMap &properties, const char *key) {
    const auto text = property_text(properties, key);
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end != text.data() ? value : 0;
}
Json filtered_ids(const Json &values, const std::set<std::string> &valid) {
    Json out = Json::array(); std::set<std::string> seen;
    for (const auto &value : values) if (value.is_string()) {
        auto id = value.get<std::string>();
        if (valid.contains(id) && seen.insert(id).second) out.push_back(id);
    }
    return out;
}
}
Json Library::defaults() {
    return {{"version", 1}, {"tracks", Json::array()}, {"folders", Json::array()}, {"favorites", Json::array()}, {"playlists", Json::array()},
        {"settings", {{"volume", 0.7}, {"repeat", "off"}, {"shuffle", false}, {"lyricOffset", 0}, {"theme", "dark"}, {"titleBar", "direct2d"}}}};
}
Library::Library(fs::path directory) : directory_(std::move(directory)), state_(defaults()) {}
Json Library::load() {
    std::lock_guard lock(mutex_);
    fs::create_directories(directory_);
    const auto file = directory_ / L"library.json";
    if (!fs::exists(file)) return state_;
    try {
        auto saved = Json::parse(read_text(file));
        if (saved.value("version", 0) != 1) throw std::runtime_error("不支持的音乐库版本");
        for (auto key : {"tracks", "folders", "favorites", "playlists"}) if (!saved.contains(key) || !saved[key].is_array()) throw std::runtime_error("音乐库字段无效");
        for (auto &track : saved["tracks"]) if (!track.is_object() || !track.contains("id") || !track["id"].is_string() || !track.contains("path") || !track["path"].is_string()) throw std::runtime_error("歌曲记录无效");
        if (!saved.contains("settings") || !saved["settings"].is_object()) throw std::runtime_error("音乐库设置无效");
        auto settings = defaults()["settings"]; settings.update(saved["settings"]);
        saved["settings"] = settings; state_ = std::move(saved);
    } catch (const std::exception &) {
        auto name = "library.corrupt-" + std::to_string(timestamp()) + ".json";
        fs::copy_file(file, directory_ / name, fs::copy_options::none);
        state_ = defaults(); state_["notice"] = "音乐库文件损坏，原文件已备份到 " + name + "。请重新导入音乐。";
    }
    rebuild_index_locked();
    // Revisit existing libraries once after the parser changes, preserving
    // cached display information while the normal background queue repairs it.
    for (auto &track : state_["tracks"]) {
        if (track.value("metadataParserVersion", 0U) != metadata_parser_version &&
            !track.value("metadataPending", false)) {
            track["metadataPending"] = true;
            track["metadataRevision"] = ++metadata_revision_;
        }
    }
    return state_;
}
void Library::rebuild_index_locked() {
    track_index_.clear();
    for (size_t i = 0; i < state_["tracks"].size(); ++i) {
        const auto &t = state_["tracks"][i];
        track_index_[t["id"].get<std::string>()] = i;
        metadata_revision_ = std::max(metadata_revision_, t.value("metadataRevision", 0ULL));
    }
}
Json Library::snapshot() const { std::lock_guard lock(mutex_); return state_; }
Json Library::settings() const { std::lock_guard lock(mutex_); return state_["settings"]; }
void Library::save_locked() { write_atomic(directory_ / L"library.json", state_.dump(2, ' ', false, Json::error_handler_t::replace)); }
Json Library::track(const std::string &id) const {
    std::lock_guard lock(mutex_);
    const auto found = track_index_.find(id);
    if (found != track_index_.end()) return state_["tracks"][found->second];
    throw std::runtime_error("歌曲不在音乐库中");
}
Json Library::import_paths(const std::vector<fs::path> &inputs, std::function<void(Json)> progress, std::stop_token stop) {
    auto before = snapshot();
    std::map<std::string, fs::path> files;
    std::set<std::string> visited, roots;
    for (const auto &root : before["folders"]) roots.insert(root.get<std::string>());
    Json warnings = Json::array();
    size_t discovered = 0;
    auto last_progress = std::chrono::steady_clock::now();
    // Resolve selected roots once. Entries below them already have absolute,
    // canonical parents; do not reopen every path component for every file.
    std::function<void(const fs::path &, DWORD)> walk = [&](const fs::path &input, DWORD attributes) {
        if (stop.stop_requested()) throw std::runtime_error("扫描已取消");
        try {
            const auto key = stored_path_key(input);
            if (!visited.insert(key).second) return;
            if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
                for (const auto &entry : fs::directory_iterator(input, fs::directory_options::skip_permission_denied)) {
                    const auto attr = GetFileAttributesW(entry.path().c_str());
                    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_REPARSE_POINT)) walk(entry.path(), attr);
                }
            } else if (audio_extensions.contains(lower(path_text(input.extension())))) {
                files.emplace(key, input); ++discovered;
                const auto now = std::chrono::steady_clock::now();
                if (progress && now - last_progress >= std::chrono::milliseconds(100)) {
                    progress({{"done", discovered}, {"total", 0}, {"name", path_text(input.filename())}}); last_progress = now;
                }
            }
        } catch (const fs::filesystem_error &) { warnings.push_back("无法访问：" + path_text(input)); }
    };
    for (const auto &input : inputs) {
        try {
            const auto real = fs::canonical(input);
            const auto attributes = GetFileAttributesW(real.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) { warnings.push_back("无法访问：" + path_text(input)); continue; }
            if (attributes & FILE_ATTRIBUTE_DIRECTORY) roots.insert(path_text(real));
            walk(real, attributes);
        } catch (const fs::filesystem_error &) { warnings.push_back("无法访问：" + path_text(input)); }
    }
    std::map<std::string, Json> tracks;
    std::map<std::string, std::string> previous_ids;
    for (const auto &t : before["tracks"]) {
        const auto id = t["id"].get<std::string>(); tracks[id] = t;
        previous_ids[stored_path_key(from_text(t["path"]))] = id;
    }
    Json pending = Json::array();
    for (const auto &[key, file] : files) {
        if (stop.stop_requested()) throw std::runtime_error("扫描已取消");
        const auto id = previous_ids.contains(key) ? previous_ids[key] : track_id(file);
        Json t;
        if (tracks.contains(id)) t = tracks[id];
        else {
            auto format = path_text(file.extension()).substr(1);
            std::transform(format.begin(), format.end(), format.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            t = {{"id", id}, {"path", path_text(file)}, {"title", path_text(file.stem())}, {"artist", "未知艺术家"}, {"album", "未知专辑"},
                {"duration", 0}, {"format", format}, {"addedAt", timestamp()}};
        }
        t["metadataPending"] = true; t["metadataRevision"] = ++metadata_revision_;
        pending.push_back({{"id", id}, {"revision", metadata_revision_}});
        tracks[id] = std::move(t);
    }
    {
        std::lock_guard lock(mutex_);
        auto next = state_; next["tracks"] = Json::array();
        for (auto &[id, t] : tracks) next["tracks"].push_back(std::move(t));
        next["folders"] = roots;
        write_atomic(directory_ / L"library.json", next.dump(2, ' ', false, Json::error_handler_t::replace));
        state_ = std::move(next); rebuild_index_locked();
    }
    return {{"library", snapshot()}, {"imported", files.size()}, {"warnings", warnings}, {"pending", pending}};
}
Json Library::pending_metadata() const {
    std::lock_guard lock(mutex_); Json pending = Json::array();
    for (const auto &t : state_["tracks"]) if (t.value("metadataPending", false))
        pending.push_back({{"id", t["id"]}, {"revision", t["metadataRevision"]}});
    return pending;
}
Json Library::enrich_metadata(const Json &candidate) {
    const auto id = candidate.at("id").get<std::string>();
    Json t;
    {
        std::lock_guard lock(mutex_);
        const auto found = track_index_.find(id);
        if (found == track_index_.end()) return nullptr;
        t = state_["tracks"][found->second];
        if (!t.value("metadataPending", false) || t["metadataRevision"] != candidate.at("revision")) return nullptr;
    }
    const auto file = from_text(t["path"]);
    TagLib::FileRef ref;
    t.erase("metadataError");
    try {
        t["size"] = fs::file_size(file);
        // Fast duration estimates avoid a full audio scan. Playback obtains
        // the exact duration from the media element when the track is opened.
        ref = TagLib::FileRef(file.c_str(), true, TagLib::AudioProperties::Fast);
        if (ref.isNull()) throw std::runtime_error("无法读取音频标签");
        if (auto tag = ref.tag()) {
            t["title"] = string_or(tag->title(), path_text(file.stem()));
            t["artist"] = string_or(tag->artist(), "未知艺术家"); t["album"] = string_or(tag->album(), "未知专辑");
        }
        if (auto p = ref.audioProperties()) { t["duration"] = p->lengthInMilliseconds() / 1000.0; t["sampleRate"] = p->sampleRate(); t["bitrate"] = p->bitrate() * 1000; }
        const auto properties = ref.properties();
        const auto album_artist = property_text(properties, "ALBUMARTIST");
        const auto artists = property_text(properties, "ARTIST");
        if (!album_artist.empty()) t["albumArtist"] = album_artist; else t.erase("albumArtist");
        if (!artists.empty()) t["artist"] = artists;
        else if (t.value("artist", "未知艺术家") == "未知艺术家" && !album_artist.empty()) t["artist"] = album_artist;
        for (const auto &[field, key] : std::vector<std::pair<const char *, const char *>>{
                {"trackNumber", "TRACKNUMBER"}, {"discNumber", "DISCNUMBER"}, {"year", "DATE"}}) {
            const auto value = property_number(properties, key);
            if (value) t[field] = value; else t.erase(field);
        }
        t.erase("embedded");
        if (properties.contains("LYRICS")) t["embedded"] = properties["LYRICS"].toString("\n").to8Bit(true);
    } catch (const std::exception &error) { t["metadataError"] = "无法读取标签，已保留已有信息：" + std::string(error.what()); }
    // A tag failure must not prevent finding a local external cover.
    const auto cover = find_cover(file, ref.isNull() ? nullptr : &ref);
    std::lock_guard lock(mutex_);
    const auto found = track_index_.find(id);
    if (found == track_index_.end()) return nullptr;
    auto &current = state_["tracks"][found->second];
    if (current["metadataRevision"] != candidate.at("revision")) return nullptr;
    // A removal or a newer import invalidates old work before it can write a
    // cover or put a track back. Keep lyrics attached during the read as well.
    if (current.contains("lyricPath")) t["lyricPath"] = current["lyricPath"];
    if (cover.image) {
        try {
            write_atomic(directory_ / (id + ".cover"), cover.image->bytes);
            t["cover"] = cover.image->mime; t["coverSource"] = cover.image->source;
            t["coverRevision"] = t["metadataRevision"];
            t.erase("coverError");
        } catch (const std::exception &error) { t["coverError"] = "无法缓存封面：" + std::string(error.what()); }
    } else if (!cover.error.empty()) {
        t["coverError"] = cover.error;
    } else if (!t.contains("metadataError")) {
        if (t.contains("cover")) { std::error_code error; fs::remove(directory_ / (id + ".cover"), error); }
        for (auto field : {"cover", "coverSource", "coverRevision", "coverError"}) t.erase(field);
    }
    t["metadataParserVersion"] = metadata_parser_version;
    t["metadataPending"] = false; current = t; metadata_dirty_ = true;
    return t;
}
void Library::flush_metadata() {
    std::lock_guard lock(mutex_);
    if (metadata_dirty_) { save_locked(); metadata_dirty_ = false; }
}
Json Library::update(const Json &patch) {
    std::lock_guard lock(mutex_);
    if (!patch.is_object()) throw std::runtime_error("无效的设置");
    auto next = state_; std::set<std::string> ids;
    for (const auto &track : state_["tracks"]) ids.insert(track["id"].get<std::string>());
    if (patch.contains("favorites") && patch["favorites"].is_array()) next["favorites"] = filtered_ids(patch["favorites"], ids);
    if (patch.contains("playlists") && patch["playlists"].is_array()) {
        if (patch["playlists"].size() > 1000) throw std::runtime_error("歌单数量超过上限");
        Json lists = Json::array();
        for (const auto &p : patch["playlists"]) {
            if (!p.is_object() || !p.contains("id") || !p["id"].is_string() || !p.contains("name") || !p["name"].is_string() || !p.contains("trackIds") || !p["trackIds"].is_array()) throw std::runtime_error("无效的歌单");
            auto name = p["name"].get<std::string>(); if (name.empty() || name.size() > 400) throw std::runtime_error("无效的歌单名称");
            lists.push_back({{"id", p["id"]}, {"name", name}, {"trackIds", filtered_ids(p["trackIds"], ids)}});
        }
        next["playlists"] = lists;
    }
    if (patch.contains("settings") && patch["settings"].is_object()) {
        const auto &s = patch["settings"]; auto &target = next["settings"];
        if (s.contains("volume") && s["volume"].is_number()) target["volume"] = std::clamp(s["volume"].get<double>(), 0.0, 1.0);
        if (s.contains("lyricOffset") && s["lyricOffset"].is_number()) target["lyricOffset"] = std::clamp(s["lyricOffset"].get<double>(), -30000.0, 30000.0);
        if (s.contains("shuffle") && s["shuffle"].is_boolean()) target["shuffle"] = s["shuffle"];
        for (const auto &[key, choices] : std::map<std::string, std::set<std::string>>{{"repeat", {"off", "all", "one"}}, {"theme", {"dark", "light"}}, {"titleBar", {"direct2d", "system", "webview2"}}})
            if (s.contains(key) && s[key].is_string() && choices.contains(s[key].get<std::string>())) target[key] = s[key];
    }
    write_atomic(directory_ / L"library.json", next.dump(2)); state_ = std::move(next); return state_;
}
Json Library::remove(const std::string &id) {
    (void)track(id); std::lock_guard lock(mutex_);
    auto remove_id = [&](Json &ids) { ids.erase(std::remove(ids.begin(), ids.end(), Json(id)), ids.end()); };
    auto &tracks = state_["tracks"];
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&](const Json &t) { return t["id"] == id; }), tracks.end());
    remove_id(state_["favorites"]); for (auto &p : state_["playlists"]) remove_id(p["trackIds"]);
    rebuild_index_locked(); save_locked(); return state_;
}
Json Library::remove_folder(const std::string &folder) {
    std::lock_guard lock(mutex_);
    auto key = stored_path_key(from_text(folder));
    const auto &folders = state_["folders"];
    auto registered = [&] {
        return std::any_of(folders.begin(), folders.end(), [&](const Json &value) {
            return value.is_string() && stored_path_key(from_text(value.get<std::string>())) == key;
        });
    };
    if (!registered()) {
        // API callers may pass an alias of an imported root. Only resolve
        // that one input; normal UI requests use the stored canonical path.
        key = path_key(from_text(folder));
        if (!registered()) throw std::runtime_error("音乐文件夹不在音乐库中");
    }
    const auto prefix = key.ends_with('\\') ? key : key + '\\';
    auto inside = [&](const std::string &path) {
        const auto normalized = stored_path_key(from_text(path));
        return normalized == key || normalized.starts_with(prefix);
    };
    auto next = state_;
    auto &next_folders = next["folders"];
    next_folders.erase(std::remove_if(next_folders.begin(), next_folders.end(),
        [&](const Json &value) { return inside(value.get<std::string>()); }), next_folders.end());
    std::unordered_set<std::string> ids;
    std::vector<std::string> covers;
    auto &tracks = next["tracks"];
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&](const Json &t) {
        if (!inside(t["path"].get<std::string>())) return false;
        auto id = t["id"].get<std::string>(); ids.insert(id);
        if (t.contains("cover")) covers.push_back(id);
        return true;
    }), tracks.end());
    auto prune = [&](Json &values) {
        values.erase(std::remove_if(values.begin(), values.end(),
            [&](const Json &value) { return ids.contains(value.get<std::string>()); }), values.end());
    };
    prune(next["favorites"]); for (auto &playlist : next["playlists"]) prune(playlist["trackIds"]);
    // Keep the old state intact if persistence fails, so the UI can recover.
    write_atomic(directory_ / L"library.json", next.dump(2, ' ', false, Json::error_handler_t::replace));
    state_ = std::move(next); rebuild_index_locked();
    std::error_code error;
    for (const auto &id : covers) fs::remove(directory_ / (id + ".cover"), error);
    return state_;
}
Json Library::lyrics(const std::string &id) const {
    auto t = track(id); auto audio = from_text(t["path"]); auto stem = audio; stem.replace_extension();
    std::vector<fs::path> candidates;
    if (t.contains("lyricPath") && t["lyricPath"].is_string()) candidates.push_back(from_text(t["lyricPath"]));
    else for (auto ext : {L".ttml", L".yrc", L".lrc", L".txt"}) { auto p = stem; p += ext; candidates.push_back(p); }
    for (const auto &p : candidates) if (fs::exists(p)) return {{"text", read_text(p, 5 * 1024 * 1024)}, {"format", lower(path_text(p.extension()).substr(1))}, {"source", path_text(p.filename())}};
    const auto embedded = t.value("embedded", "");
    return {{"text", embedded}, {"format", "lrc"}, {"source", embedded.empty() ? "" : "内嵌歌词"}};
}
Json Library::attach_lyrics(const std::string &id, const fs::path &file) {
    if (!lyric_extensions.contains(lower(path_text(file.extension())))) throw std::runtime_error("不支持的歌词格式");
    (void)read_text(file, 5 * 1024 * 1024); (void)track(id);
    { std::lock_guard lock(mutex_); for (auto &t : state_["tracks"]) if (t["id"] == id) t["lyricPath"] = path_text(fs::canonical(file)); save_locked(); }
    return lyrics(id);
}
}
