#include "library.h"
#include "cover.h"
#include "fonts.h"
#include "online_lyrics.h"
#include "index_translate.h"
#include "cue.h"
#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>
#include <tvariant.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <unordered_set>
#include <chrono>
#include <charconv>
#include <cstdio>
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
/** The file's last write time in Unix milliseconds (0 when it cannot be read): 修改日期 sorting. */
long long modified_ms(const fs::path &file) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data)) return 0;
    const auto ticks = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    return ticks < 116444736000000000ULL ? 0 : static_cast<long long>((ticks - 116444736000000000ULL) / 10000);
}
/** Every known lyric provider once: the saved order first (unknown ids dropped), providers added later after it. */
Json provider_order(const Json &saved) {
    const auto &known = OnlineLyrics::provider_ids();
    Json order = Json::array();
    if (saved.is_array()) for (const auto &id : saved)
        if (id.is_string() && std::find(known.begin(), known.end(), id.get<std::string>()) != known.end() && std::find(order.begin(), order.end(), id) == order.end()) order.push_back(id);
    for (const auto &id : known) if (std::find(order.begin(), order.end(), Json(id)) == order.end()) order.push_back(id);
    return order;
}
/**
 * Providers added after the user last saved their selection start enabled (as in BetterLyrics'
 * mergePreferredProviders); ones they switched off stay off. Libraries from before
 * "lyricProvidersSeen" existed had seen the first six providers.
 */
void enable_new_providers(Json &settings, const Json &saved) {
    const auto &known = OnlineLyrics::provider_ids();
    Json seen = saved.contains("lyricProvidersSeen") && saved["lyricProvidersSeen"].is_array() ? saved["lyricProvidersSeen"]
        : Json{"amll", "qq", "netease", "kugou", "lrclib", "apple"};
    Json enabled = settings["lyricProviders"].is_array() ? settings["lyricProviders"] : Json::array();
    Json providers = Json::array();
    for (const auto &id : known)
        if (std::find(enabled.begin(), enabled.end(), Json(id)) != enabled.end() || std::find(seen.begin(), seen.end(), Json(id)) == seen.end()) providers.push_back(id);
    settings["lyricProviders"] = providers;
    settings["lyricProvidersSeen"] = known;
}
/**
 * Translation settings from before 哔哩哔哩 Index-Translate replaced the local Ollama model: the local
 * provider becomes Index-Translate, an Ollama model name its default model, the Ollama address goes.
 */
void migrate_translation(Json &settings) {
    if (settings.value("translateProvider", "") != "niutrans") settings["translateProvider"] = "bilibili";
    if (!settings.contains("translateModel") || !settings["translateModel"].is_string() || !IndexTranslate::valid_model(settings["translateModel"].get<std::string>())) settings["translateModel"] = IndexTranslate::kDefaultModel;
    settings.erase("translateEndpoint");
}
}
Json Library::defaults() {
    return {{"version", 1}, {"tracks", Json::array()}, {"folders", Json::array()}, {"favorites", Json::array()}, {"playlists", Json::array()},
        {"settings", {{"volume", 0.7}, {"repeat", "off"}, {"shuffle", false}, {"lyricOffset", 0}, {"theme", "dark"}, {"titleBar", "direct2d"}, {"lyricFont", ""}, {"lyricWeight", 0}, {"lyricScale", 1}, {"chineseSearch", false}, {"lyricScript", "o"}, {"lyricVariant", "t"}, {"lyricRuby", ""}, {"lyricRubyBelow", false}, {"lyricTranslate", ""}, {"translateProvider", "bilibili"}, {"translateModel", IndexTranslate::kDefaultModel}, {"onlineLyrics", true}, {"onlineTranslation", true}, {"onlineRomaji", true}, {"onlineJyutping", true}, {"lyricSource", "local"}, {"lyricProviders", OnlineLyrics::provider_ids()}, {"lyricProvidersSeen", OnlineLyrics::provider_ids()}, {"lyricProviderOrder", OnlineLyrics::provider_ids()}, {"amllMirror", ""}, {"librarySort", "added"}, {"titleSource", "tag"}}}};
}
Library::Library(fs::path directory) : directory_(std::move(directory)), state_(defaults()) {}
Json Library::load() {
    std::lock_guard lock(mutex_);
    cover_cache_.clear(); cover_bytes_.clear(); lyric_cache_.clear();
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
        enable_new_providers(settings, saved["settings"]);
        migrate_translation(settings);
        settings["lyricProviderOrder"] = provider_order(settings.value("lyricProviderOrder", Json::array()));
        // Libraries from before 修改日期 sorting: read each file's time once (saved with the next write).
        for (auto &track : saved["tracks"])
            if (!track.contains("modifiedAt")) if (const auto modified = modified_ms(from_text(track["path"].get<std::string>()))) track["modifiedAt"] = modified;
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
fs::path Library::audio_path(const std::string &id) const {
    std::lock_guard lock(mutex_);
    const auto found = track_index_.find(id);
    if (found == track_index_.end()) throw std::runtime_error("歌曲不在音乐库中");
    return from_text(state_["tracks"][found->second]["path"]);
}
std::shared_ptr<const CoverResource> Library::cover_resource(const std::string &id, const std::string &revision) const {
    // A cache hit performs no filesystem work and never copies embedded lyrics
    // or image bytes. Every request still validates membership and revision.
    std::lock_guard lock(mutex_);
    const auto found = track_index_.find(id);
    if (found == track_index_.end()) return {};
    const auto &track = state_["tracks"][found->second];
    if (!track.contains("cover")) return {};
    const auto version = track.value("coverRevision", 0ULL);
    if (!revision.empty() && revision != std::to_string(version) && revision != track.value("coverHash", "")) return {};
    if (auto cached = cover_bytes_.get(id); cached && cached->revision == version) return cached;
    const auto file = directory_ / (id + ".cover");
    std::error_code error;
    const auto size = fs::file_size(file, error);
    if (error || !size || size > 64 * 1024 * 1024) return {};
    std::ifstream input(file, std::ios::binary);
    auto data = std::make_shared<CoverResource>();
    data->bytes.resize(static_cast<size_t>(size));
    if (!input.read(data->bytes.data(), static_cast<std::streamsize>(size))) return {};
    data->mime = track["cover"].get<std::string>(); data->revision = version; data->hash = track.value("coverHash", "");
    cover_bytes_.put(id, data, data->bytes.size() + 128);
    return data;
}
Json Library::import_paths(const std::vector<fs::path> &inputs, std::function<void(Json)> progress, std::stop_token stop, bool prune_missing) {
    cover_cache_.clear_directories();
    auto before = snapshot();
    std::map<std::string, fs::path> files;
    std::set<std::string> visited, roots;
    std::vector<std::string> walked, unreadable; // stored path keys of read roots / folders that failed to list
    for (const auto &root : before["folders"]) roots.insert(root.get<std::string>());
    Json warnings = Json::array();
    size_t discovered = 0;
    std::vector<fs::path> cues;
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
            } else if (lower(path_text(input.extension())) == ".cue") cues.push_back(input);
        } catch (const fs::filesystem_error &) { warnings.push_back("无法访问：" + path_text(input)); unreadable.push_back(stored_path_key(input)); }
    };
    for (const auto &input : inputs) {
        try {
            const auto real = fs::canonical(input);
            const auto attributes = GetFileAttributesW(real.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) { warnings.push_back("无法访问：" + path_text(input)); continue; }
            if (attributes & FILE_ATTRIBUTE_DIRECTORY) { roots.insert(path_text(real)); walked.push_back(stored_path_key(real)); }
            walk(real, attributes);
        } catch (const fs::filesystem_error &) { warnings.push_back("无法访问：" + path_text(input)); }
    }
    // Whole-CD rips: a .cue whose audio file was found splits that file into its tracks. The file
    // itself is then not a song of its own (formats the decoder cannot play are never found).
    struct CueEntry { std::string id; fs::path audio, sheet; CueSheet info; CueTrack track; };
    std::vector<CueEntry> cue_entries;
    std::set<std::string> cue_audio;
    for (const auto &sheet_path : cues) {
        if (stop.stop_requested()) throw std::runtime_error("扫描已取消");
        CueSheet sheet;
        try { sheet = read_cue(sheet_path); } catch (const std::exception &) { warnings.push_back("无法读取 CUE：" + path_text(sheet_path)); continue; }
        for (const auto &file : sheet.files) {
            // The sheet often names the rip's original file (e.g. a .wav later converted to .flac): try the same name with each audio extension.
            std::string key = stored_path_key(file.audio);
            if (!files.contains(key)) {
                key.clear();
                for (const auto &extension : audio_extensions) { auto candidate = file.audio; candidate.replace_extension(from_text(extension)); if (files.contains(stored_path_key(candidate))) { key = stored_path_key(candidate); break; } }
                if (key.empty() && sheet.files.size() == 1)
                    for (const auto &extension : audio_extensions) { auto candidate = sheet_path; candidate.replace_extension(from_text(extension)); if (files.contains(stored_path_key(candidate))) { key = stored_path_key(candidate); break; } }
            }
            if (key.empty() || cue_audio.contains(key)) continue;
            cue_audio.insert(key);
            const auto audio = files.at(key);
            for (const auto &track : file.tracks) {
                char number[8]; std::snprintf(number, sizeof(number), "%02d", track.number);
                cue_entries.push_back({track_id(audio) + "-" + number, audio, sheet_path, sheet, track});
            }
        }
    }
    for (const auto &key : cue_audio) files.erase(key);
    std::map<std::string, Json> tracks;
    std::map<std::string, std::string> previous_ids;
    std::unordered_set<std::string> removed;
    for (const auto &t : before["tracks"]) {
        const auto id = t["id"].get<std::string>(); const auto key = stored_path_key(from_text(t["path"]));
        // A file now split by its .cue is replaced by the sheet's tracks.
        if (!t.contains("cue") && cue_audio.contains(key)) { removed.insert(id); continue; }
        tracks[id] = t;
        if (!t.contains("cue")) previous_ids[key] = id;
    }
    Json pending = Json::array();
    // One time for the whole import: songs added together tie, and the list orders them by path.
    const auto added = timestamp();
    auto format_of = [](const fs::path &file) {
        auto format = path_text(file.extension()).substr(1);
        std::transform(format.begin(), format.end(), format.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return format;
    };
    auto queue = [&](Json t, const fs::path &file) {
        if (const auto modified = modified_ms(file)) t["modifiedAt"] = modified;
        t["metadataPending"] = true; t["metadataRevision"] = ++metadata_revision_;
        pending.push_back({{"id", t["id"]}, {"revision", metadata_revision_}});
        const auto id = t["id"].get<std::string>(); // read before the move: json's operator= takes its argument by value
        tracks[id] = std::move(t);
    };
    for (const auto &[key, file] : files) {
        if (stop.stop_requested()) throw std::runtime_error("扫描已取消");
        const auto id = previous_ids.contains(key) ? previous_ids[key] : track_id(file);
        Json t;
        if (tracks.contains(id)) t = tracks[id];
        else t = {{"id", id}, {"path", path_text(file)}, {"title", path_text(file.stem())}, {"artist", "未知艺术家"}, {"album", "未知专辑"},
                  {"duration", 0}, {"format", format_of(file)}, {"addedAt", added}};
        queue(std::move(t), file);
    }
    std::set<std::string> cue_ids;
    for (const auto &entry : cue_entries) {
        if (!cue_ids.insert(entry.id).second) continue;
        Json t = tracks.contains(entry.id) ? tracks[entry.id] : Json{{"id", entry.id}, {"addedAt", added}};
        // The sheet is the song's tags: re-read on every scan so edits to the .cue show up.
        const auto &track = entry.track; const auto &sheet = entry.info;
        t["path"] = path_text(entry.audio); t["format"] = format_of(entry.audio);
        t["title"] = track.title.empty() ? "Track " + std::to_string(track.number) : track.title;
        t["artist"] = !track.performer.empty() ? track.performer : !sheet.performer.empty() ? sheet.performer : "未知艺术家";
        t["album"] = sheet.title.empty() ? path_text(entry.sheet.stem()) : sheet.title;
        if (!sheet.performer.empty()) t["albumArtist"] = sheet.performer; else t.erase("albumArtist");
        t["trackNumber"] = track.number;
        if (sheet.disc) t["discNumber"] = sheet.disc; else t.erase("discNumber");
        if (sheet.year) t["year"] = sheet.year; else t.erase("year");
        t["duration"] = track.end > 0 ? track.end - track.start : t.value("duration", 0.0);
        t["cue"] = {{"start", track.start}, {"end", track.end}, {"sheet", path_text(entry.sheet)}};
        queue(std::move(t), entry.audio);
    }
    // Rescan drops tracks whose files moved or were deleted, but only below folders that were read in
    // full: an unplugged drive or an unreadable subfolder must not empty the library.
    auto under = [](const std::string &key, const std::vector<std::string> &prefixes) {
        return std::any_of(prefixes.begin(), prefixes.end(), [&](const std::string &p) { return key == p || key.starts_with(p.ends_with('\\') ? p : p + '\\'); });
    };
    if (prune_missing) {
        for (auto it = tracks.begin(); it != tracks.end();) {
            const auto key = stored_path_key(from_text(it->second["path"]));
            const bool present = it->second.contains("cue") ? cue_ids.contains(it->first) : files.contains(key);
            if (!present && under(key, walked) && !under(key, unreadable)) { removed.insert(it->first); it = tracks.erase(it); }
            else ++it;
        }
    }
    {
        std::lock_guard lock(mutex_);
        auto next = state_; next["tracks"] = Json::array();
        for (auto &[id, t] : tracks) next["tracks"].push_back(std::move(t));
        next["folders"] = roots;
        if (!removed.empty()) {
            auto prune = [&](Json &values) { values.erase(std::remove_if(values.begin(), values.end(), [&](const Json &v) { return removed.contains(v.get<std::string>()); }), values.end()); };
            prune(next["favorites"]); for (auto &playlist : next["playlists"]) prune(playlist["trackIds"]);
            for (const auto &id : removed) { cover_bytes_.erase(id); lyric_cache_.erase(id); }
        }
        write_atomic(directory_ / L"library.json", next.dump(2, ' ', false, Json::error_handler_t::replace));
        state_ = std::move(next); rebuild_index_locked();
    }
    if (!removed.empty()) { cover_cache_.clear(); std::error_code error; for (const auto &id : removed) fs::remove(directory_ / (id + ".cover"), error); }
    return {{"library", snapshot()}, {"imported", files.size() + cue_ids.size()}, {"removed", removed.size()}, {"warnings", warnings}, {"pending", pending}};
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
    const auto audio_stamp = file_stamp(file);
    const auto inputs = cover_cache_.inputs(file);
    const auto fingerprint = audio_stamp && inputs.valid ? content_hash(*audio_stamp + ":" + inputs.fingerprint) : "";
    std::error_code cache_error;
    const bool cover_available = !t.contains("cover") || fs::is_regular_file(directory_ / (id + ".cover"), cache_error);
    if (!fingerprint.empty() && t.value("metadataParserVersion", 0U) == metadata_parser_version &&
        t.value("metadataFingerprint", "") == fingerprint && !t.contains("metadataError") &&
        !t.contains("coverError") && cover_available) {
        std::lock_guard lock(mutex_);
        const auto found = track_index_.find(id);
        if (found == track_index_.end()) return nullptr;
        auto &current = state_["tracks"][found->second];
        if (current["metadataRevision"] != candidate.at("revision")) return nullptr;
        current["metadataPending"] = false; metadata_dirty_ = true;
        return current;
    }
    TagLib::FileRef ref;
    t.erase("metadataError");
    try {
        t["size"] = fs::file_size(file);
        if (const auto modified = modified_ms(file)) t["modifiedAt"] = modified;
        // Fast duration estimates avoid a full audio scan. Playback obtains
        // the exact duration from the media element when the track is opened.
        ref = TagLib::FileRef(file.c_str(), true, TagLib::AudioProperties::Fast);
        if (ref.isNull()) throw std::runtime_error("无法读取音频标签");
        // A CUE track's tags come from its sheet; the shared file gives only the audio properties and length.
        const auto cue = t.value("cue", Json());
        if (cue.is_object()) {
            if (auto p = ref.audioProperties()) {
                const double end = cue.value("end", 0.0), start = cue.value("start", 0.0);
                t["duration"] = end > 0 ? end - start : std::max(0.0, p->lengthInMilliseconds() / 1000.0 - start);
                t["sampleRate"] = p->sampleRate(); t["bitrate"] = p->bitrate() * 1000;
            }
            t.erase("embedded");
        } else {
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
        }
    } catch (const std::exception &error) { t["metadataError"] = "无法读取标签，已保留已有信息：" + std::string(error.what()); }
    // A tag failure must not prevent finding a local external cover.
    const auto cover = cover_cache_.find(inputs, ref.isNull() ? nullptr : &ref);
    // Never save a successful cache key for an audio file changed while read.
    const bool stable = audio_stamp && file_stamp(file) == audio_stamp;
    std::lock_guard lock(mutex_);
    const auto found = track_index_.find(id);
    if (found == track_index_.end()) return nullptr;
    auto &current = state_["tracks"][found->second];
    if (current["metadataRevision"] != candidate.at("revision")) return nullptr;
    // A removal or a newer import invalidates old work before it can write a
    // cover or put a track back. Keep lyrics attached during the read as well.
    if (current.contains("lyricPath")) t["lyricPath"] = current["lyricPath"];
    for (const auto *field : {"lyricRuby", "lyricPreference"}) {
        if (current.contains(field)) t[field] = current[field]; else t.erase(field);
    }
    if (cover.image) {
        try {
            const auto hash = content_hash(cover.image->bytes);
            const bool unchanged = t.value("coverHash", "") == hash && t.contains("coverRevision");
            if (!unchanged || !cover_available) write_atomic(directory_ / (id + ".cover"), cover.image->bytes);
            t["cover"] = cover.image->mime; t["coverSource"] = cover.source; t["coverHash"] = hash;
            if (!unchanged) t["coverRevision"] = t["metadataRevision"];
            auto resource = std::make_shared<CoverResource>(CoverResource{cover.image->bytes, cover.image->mime, t["coverRevision"].get<unsigned long long>(), hash});
            cover_bytes_.put(id, resource, resource->bytes.size() + 128);
            t.erase("coverError");
        } catch (const std::exception &error) { t["coverError"] = "无法缓存封面：" + std::string(error.what()); }
    } else if (!cover.error.empty()) {
        t["coverError"] = cover.error;
    } else if (!t.contains("metadataError")) {
        if (t.contains("cover")) { std::error_code error; fs::remove(directory_ / (id + ".cover"), error); }
        for (auto field : {"cover", "coverSource", "coverRevision", "coverHash", "coverError"}) t.erase(field);
        cover_bytes_.erase(id);
    }
    if (stable && !fingerprint.empty() && !t.contains("metadataError") && !t.contains("coverError")) t["metadataFingerprint"] = fingerprint;
    else t.erase("metadataFingerprint");
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
        // 原 / 简 / 繁 for lyrics, and the traditional standard 繁 uses (kept while 原 / 简 is shown).
        if (s.contains("lyricScript") && s["lyricScript"].is_string()) { const auto v = s["lyricScript"].get<std::string>(); if (v == "o" || v == "s" || v == "t") target["lyricScript"] = v; }
        if (s.contains("lyricVariant") && s["lyricVariant"].is_string()) { const auto v = s["lyricVariant"].get<std::string>(); if (v == "t" || v == "tw" || v == "twp" || v == "hk") target["lyricVariant"] = v; }
        // 注音: none or Cantonese Jyutping, above each character or on a line of its own.
        if (s.contains("lyricRuby") && s["lyricRuby"].is_string()) { const auto v = s["lyricRuby"].get<std::string>(); if (v.empty() || v == "jyutping") target["lyricRuby"] = v; }
        if (s.contains("lyricRubyBelow") && s["lyricRubyBelow"].is_boolean()) target["lyricRubyBelow"] = s["lyricRubyBelow"];
        // 译: 哔哩哔哩 Index-Translate (one of its models) or 小牛翻译, into Chinese / English.
        if (s.contains("translateProvider") && s["translateProvider"].is_string()) { const auto v = s["translateProvider"].get<std::string>(); if (v == "bilibili" || v == "niutrans") target["translateProvider"] = v; }
        if (s.contains("translateModel") && s["translateModel"].is_string() && IndexTranslate::valid_model(s["translateModel"].get<std::string>())) target["translateModel"] = s["translateModel"];
        if (s.contains("lyricTranslate") && s["lyricTranslate"].is_string()) { const auto v = s["lyricTranslate"].get<std::string>(); if (v.empty() || v == "zh" || v == "en") target["lyricTranslate"] = v; }
        // The song the player bar has ready at startup: the library's first (as listed) or a random one.
        if (s.contains("startupTrack") && s["startupTrack"].is_string()) { const auto v = s["startupTrack"].get<std::string>(); if (v == "first" || v == "random") target["startupTrack"] = v; }
        // 歌曲排序 in song lists, kept across launches.
        if (s.contains("librarySort") && s["librarySort"].is_string()) { static const std::set<std::string> sorts{"added", "modified", "title", "artist", "album", "file", "duration"}; if (const auto v = s["librarySort"].get<std::string>(); sorts.contains(v)) target["librarySort"] = v; }
        // Song names from the tags' title or from the file name.
        if (s.contains("titleSource") && s["titleSource"].is_string()) { const auto v = s["titleSource"].get<std::string>(); if (v == "tag" || v == "file") target["titleSource"] = v; }        // The library hero's sheen on hover (off unless switched on).
        if (s.contains("heroSheen") && s["heroSheen"].is_boolean()) target["heroSheen"] = s["heroSheen"];
        // Player page layout: cover and lyrics, lyrics only, or cover only.
        if (s.contains("nowLayout") && s["nowLayout"].is_string()) { const auto v = s["nowLayout"].get<std::string>(); if (v == "default" || v == "lyrics" || v == "cover") target["nowLayout"] = v; }
        if (s.contains("lyricOffset") && s["lyricOffset"].is_number()) target["lyricOffset"] = std::clamp(s["lyricOffset"].get<double>(), -30000.0, 30000.0);
        if (s.contains("shuffle") && s["shuffle"].is_boolean()) target["shuffle"] = s["shuffle"];
        if (s.contains("chineseSearch") && s["chineseSearch"].is_boolean()) target["chineseSearch"] = s["chineseSearch"];
        if (s.contains("onlineLyrics") && s["onlineLyrics"].is_boolean()) target["onlineLyrics"] = s["onlineLyrics"];
        // Whether an online lyric's own translation / romanization / 粤拼 shows by default (all on unless switched off).
        static constexpr const char *online_defaults[]{"onlineTranslation", "onlineRomaji", "onlineJyutping"};
        for (const char *name : online_defaults)
            if (s.contains(name) && s[name].is_boolean()) target[name] = s[name];
        if (s.contains("lyricProviders") && s["lyricProviders"].is_array()) {
            const auto &known = OnlineLyrics::provider_ids();
            Json providers = Json::array();
            for (const auto &id : known) if (std::any_of(s["lyricProviders"].begin(), s["lyricProviders"].end(), [&](const Json &p) { return p == id; })) providers.push_back(id);
            target["lyricProviders"] = providers; // known ids only, in a fixed order
        }
        // The providers' priority (歌词源顺序): every known id once, in the user's order.
        if (s.contains("lyricProviderOrder") && s["lyricProviderOrder"].is_array()) target["lyricProviderOrder"] = provider_order(s["lyricProviderOrder"]);
        // Default lyric source for songs without their own pick: the song's lyric file ("local"), the best
        // online match ("online"), the first provider in 歌词源顺序 that has one ("ordered"), or one provider
        // (falling back to the others when it has nothing).
        if (s.contains("lyricSource") && s["lyricSource"].is_string()) {
            const auto v = s["lyricSource"].get<std::string>(); const auto &known = OnlineLyrics::provider_ids();
            if (v == "local" || v == "online" || v == "ordered" || std::find(known.begin(), known.end(), v) != known.end()) target["lyricSource"] = v;
        }
        if (s.contains("amllMirror") && s["amllMirror"].is_string()) { const auto url = s["amllMirror"].get<std::string>(); if (url.empty() || OnlineLyrics::valid_base(url)) target["amllMirror"] = url; }
        // Lyric typography: "" / 0 mean "follow the app default".
        if (s.contains("lyricFont") && s["lyricFont"].is_string()) { auto font = s["lyricFont"].get<std::string>(); if (font.empty() || valid_font_family(font)) target["lyricFont"] = font; }
        if (s.contains("lyricWeight") && s["lyricWeight"].is_number_integer()) { auto weight = s["lyricWeight"].get<int>(); if (weight == 0 || (weight >= 100 && weight <= 900 && weight % 100 == 0)) target["lyricWeight"] = weight; }
        if (s.contains("lyricScale") && s["lyricScale"].is_number() && std::isfinite(s["lyricScale"].get<double>())) target["lyricScale"] = std::clamp(std::round(s["lyricScale"].get<double>() * 100) / 100, 0.7, 1.6);
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
    rebuild_index_locked(); save_locked();
    cover_bytes_.erase(id); lyric_cache_.erase(id); cover_cache_.clear();
    { std::error_code error; fs::remove(directory_ / (id + ".cover"), error); }
    return state_;
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
    for (const auto &id : ids) { cover_bytes_.erase(id); lyric_cache_.erase(id); }
    cover_cache_.clear();
    std::error_code error;
    for (const auto &id : covers) fs::remove(directory_ / (id + ".cover"), error);
    return state_;
}
Json Library::set_lyric_preference(const std::string &id, const std::string &source) {
    const auto &providers = OnlineLyrics::provider_ids();
    if (!source.empty() && source != "local" && source != "online" && std::find(providers.begin(), providers.end(), source) == providers.end()) throw std::runtime_error("无效的歌词来源");
    std::lock_guard lock(mutex_);
    const auto found = track_index_.find(id);
    if (found == track_index_.end()) throw std::runtime_error("歌曲不存在");
    auto next = state_; auto &t = next["tracks"][found->second];
    if (source.empty()) t.erase("lyricPreference"); else t["lyricPreference"] = source;
    write_atomic(directory_ / L"library.json", next.dump(2, ' ', false, Json::error_handler_t::replace));
    state_ = std::move(next);
    return state_["tracks"][found->second];
}
Json Library::set_lyric_ruby(const std::string &id, const std::string &ruby) {
    if (ruby != "" && ruby != "jyutping" && ruby != "romaji" && ruby != "kana" && ruby != "romaji-online") throw std::runtime_error("无效的歌词注音");
    std::lock_guard lock(mutex_);
    const auto found = track_index_.find(id);
    if (found == track_index_.end()) throw std::runtime_error("歌曲不存在");
    auto next = state_;
    // Empty explicitly disables ruby; an absent field follows the defaults.
    next["tracks"][found->second]["lyricRuby"] = ruby;
    write_atomic(directory_ / L"library.json", next.dump(2, ' ', false, Json::error_handler_t::replace));
    state_ = std::move(next);
    return state_["tracks"][found->second];
}
Json Library::lyrics(const std::string &id) const {
    auto t = track(id); auto audio = from_text(t["path"]); auto stem = audio; stem.replace_extension();
    std::vector<fs::path> candidates;
    if (t.contains("lyricPath") && t["lyricPath"].is_string()) candidates.push_back(from_text(t["lyricPath"]));
    else {
        // A CUE track shares its audio file with the whole disc: its lyric file is named after the song.
        if (t.contains("cue")) {
            const auto title = t.value("title", "");
            stem.clear();
            if (!title.empty() && title.find_first_of("\\/:*?\"<>|") == std::string::npos) try { stem = audio.parent_path() / from_text(title); } catch (const std::exception &) {}
        }
        if (!stem.empty()) for (auto ext : {L".ttml", L".yrc", L".lrc", L".txt"}) { auto p = stem; p += ext; candidates.push_back(p); }
    }
    for (const auto &p : candidates) if (const auto stamp = file_stamp(p)) {
        const auto signature = stored_path_key(p) + ":" + *stamp;
        if (const auto cached = lyric_cache_.get(id); cached && cached->signature == signature) return cached->result;
        auto text = read_text(p, 5 * 1024 * 1024);
        Json result{{"text", text}, {"format", lower(path_text(p.extension()).substr(1))}, {"source", path_text(p.filename())}};
        // Do not cache a partial read of a file being edited.
        if (file_stamp(p) == stamp) {
            std::lock_guard lock(mutex_);
            if (track_index_.contains(id)) lyric_cache_.put(id, std::make_shared<CachedLyrics>(CachedLyrics{signature, result}), text.size() + signature.size() + 256);
        }
        return result;
    }
    const auto embedded = t.value("embedded", "");
    return {{"text", embedded}, {"format", "lrc"}, {"source", embedded.empty() ? "" : "内嵌歌词"}};
}
Json Library::attach_lyrics(const std::string &id, const fs::path &file) {
    if (!lyric_extensions.contains(lower(path_text(file.extension())))) throw std::runtime_error("不支持的歌词格式");
    (void)read_text(file, 5 * 1024 * 1024); (void)track(id);
    { std::lock_guard lock(mutex_); for (auto &t : state_["tracks"]) if (t["id"] == id) t["lyricPath"] = path_text(fs::canonical(file)); save_locked(); lyric_cache_.erase(id); }
    return lyrics(id);
}
}
