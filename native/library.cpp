#include "library.h"
#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>
#include <tvariant.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
namespace dr {
namespace {
const std::set<std::string> audio_extensions{ ".mp3", ".flac", ".wav", ".m4a", ".aac", ".ogg", ".opus", ".webm", ".mp4" };
const std::set<std::string> lyric_extensions{ ".ttml", ".yrc", ".lrc", ".txt" };
std::string string_or(const TagLib::String &text, const std::string &fallback) { return text.isEmpty() ? fallback : text.to8Bit(true); }
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
    return state_;
}
Json Library::snapshot() const { std::lock_guard lock(mutex_); return state_; }
void Library::save_locked() { write_atomic(directory_ / L"library.json", state_.dump(2, ' ', false, Json::error_handler_t::replace)); }
Json Library::track(const std::string &id) const {
    std::lock_guard lock(mutex_);
    for (const auto &track : state_["tracks"]) if (track["id"] == id) return track;
    throw std::runtime_error("歌曲不在音乐库中");
}
Json Library::import_paths(const std::vector<fs::path> &inputs, std::function<void(Json)> progress, std::stop_token stop) {
    auto before = snapshot();
    std::map<std::string, fs::path> files;
    std::set<std::string> visited, roots;
    for (const auto &root : before["folders"]) if (root.is_string()) roots.insert(root.get<std::string>());
    Json warnings = Json::array();
    std::function<void(const fs::path &)> walk = [&](const fs::path &input) {
        if (stop.stop_requested()) throw std::runtime_error("扫描已取消");
        try {
            auto real = fs::canonical(input); auto key = path_key(real);
            if (!visited.insert(key).second) return;
            if (fs::is_directory(real)) {
                for (const auto &entry : fs::directory_iterator(real, fs::directory_options::skip_permission_denied)) {
                    auto attr = GetFileAttributesW(entry.path().c_str());
                    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_REPARSE_POINT)) walk(entry.path());
                }
            } else if (fs::is_regular_file(real) && audio_extensions.contains(lower(path_text(real.extension())))) files.emplace(key, real);
        } catch (const fs::filesystem_error &) { warnings.push_back("无法访问：" + path_text(input)); }
    };
    for (const auto &input : inputs) {
        std::error_code error;
        if (fs::is_directory(input, error)) roots.insert(path_text(fs::canonical(input)));
        walk(input);
    }
    std::map<std::string, Json> tracks;
    std::map<std::string, std::string> previous_ids;
    for (const auto &t : before["tracks"]) {
        const auto id = t["id"].get<std::string>(); tracks[id] = t;
        previous_ids[path_key(from_text(t["path"]))] = id;
    }
    size_t done = 0;
    for (const auto &[key, file] : files) {
        if (stop.stop_requested()) throw std::runtime_error("扫描已取消");
        const auto id = previous_ids.contains(key) ? previous_ids[key] : track_id(file);
        auto old = tracks.contains(id) ? tracks[id] : Json::object();
        auto format = path_text(file.extension()).substr(1);
        std::transform(format.begin(), format.end(), format.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        Json t = {{"id", id}, {"path", path_text(file)}, {"title", path_text(file.stem())}, {"artist", "未知艺术家"}, {"album", "未知专辑"},
            {"duration", 0}, {"format", format}, {"addedAt", old.value("addedAt", timestamp())}};
        if (old.contains("lyricPath")) t["lyricPath"] = old["lyricPath"];
        try {
            t["size"] = fs::file_size(file);
            // TagLib reads the audio directly in C++; no Node.js helper process.
            TagLib::FileRef ref(file.c_str(), true, TagLib::AudioProperties::Accurate);
            if (ref.isNull()) throw std::runtime_error("无法读取音频标签");
            if (auto tag = ref.tag()) {
                t["title"] = string_or(tag->title(), path_text(file.stem()));
                t["artist"] = string_or(tag->artist(), "未知艺术家"); t["album"] = string_or(tag->album(), "未知专辑");
            }
            if (auto p = ref.audioProperties()) { t["duration"] = p->lengthInMilliseconds() / 1000.0; t["sampleRate"] = p->sampleRate(); t["bitrate"] = p->bitrate() * 1000; }
            const auto properties = ref.properties();
            if (properties.contains("LYRICS")) t["embedded"] = properties["LYRICS"].toString("\n").to8Bit(true);
            const auto pictures = ref.complexProperties("PICTURE");
            if (!pictures.isEmpty()) {
                auto picture = pictures.front();
                const auto mime = picture["mimeType"].toString().to8Bit(true);
                auto data = picture["data"].toByteVector();
                if ((mime == "image/jpeg" || mime == "image/png" || mime == "image/webp") && data.size() <= 20 * 1024 * 1024) {
                    std::ofstream cover(directory_ / (id + ".cover"), std::ios::binary);
                    cover.write(data.data(), data.size()); if (cover) t["cover"] = mime;
                }
            }
        } catch (const std::exception &) { warnings.push_back("无法读取标签，已按文件名导入：" + path_text(file.filename())); }
        tracks[id] = std::move(t);
        ++done;
        if (progress) progress({{"done", done}, {"total", files.size()}, {"name", path_text(file.filename())}});
    }
    {
        std::lock_guard lock(mutex_);
        state_["tracks"] = Json::array();
        for (const auto &[id, t] : tracks) state_["tracks"].push_back(t);
        state_["folders"] = roots; save_locked();
    }
    return {{"library", snapshot()}, {"imported", done}, {"warnings", warnings}};
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
    save_locked(); return state_;
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
