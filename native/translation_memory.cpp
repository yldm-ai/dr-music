#include "translation_memory.h"
#include <algorithm>
#include <sstream>
namespace dr {
namespace {
constexpr size_t kCacheEntries = 20000;
std::vector<std::string> split_lines(const std::string &text) {
    std::vector<std::string> lines; std::istringstream in(text);
    for (std::string line; std::getline(in, line);) if (auto t = trim_text(line); !t.empty()) lines.push_back(std::move(t));
    return lines;
}
}

std::string trim_text(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

TranslationMemory::TranslationMemory(fs::path dir) : dir_(std::move(dir)) {}

std::map<std::string, std::string> &TranslationMemory::load(const std::string &key) {
    auto [it, inserted] = caches_.try_emplace(key);
    if (inserted) {
        std::error_code ec;
        const auto file = dir_ / from_text(key + ".json");
        if (fs::is_regular_file(file, ec)) {
            const auto json = Json::parse(read_text(file, 64 * 1024 * 1024), nullptr, false);
            if (json.is_object()) for (const auto &[k, v] : json.items()) if (v.is_string()) it->second[k] = v.get<std::string>();
        }
    }
    return it->second;
}

void TranslationMemory::save(const std::string &key) {
    auto &cache = caches_[key];
    while (cache.size() > kCacheEntries) cache.erase(cache.begin());
    std::error_code ec; fs::create_directories(dir_, ec);
    write_atomic(dir_ / from_text(key + ".json"), Json(cache).dump(-1, ' ', false, Json::error_handler_t::replace));
}

std::vector<std::string> TranslationMemory::translate(const std::string &key, const std::vector<std::string> &lines, const Generate &generate, size_t chunk_lines, size_t chunk_chars) {
    std::vector<std::string> todo;
    {
        std::lock_guard lock(mutex_);
        auto &cache = load(key);
        for (const auto &line : lines) if (auto t = trim_text(line); !t.empty() && !cache.contains(t) && std::find(todo.begin(), todo.end(), t) == todo.end()) todo.push_back(t);
    }
    std::map<std::string, std::string> fresh;
    for (size_t start = 0; start < todo.size();) {
        std::vector<std::string> chunk; size_t size = 0;
        while (start < todo.size() && chunk.size() < chunk_lines && (chunk.empty() || size + todo[start].size() + 1 <= chunk_chars)) { size += todo[start].size() + 1; chunk.push_back(todo[start++]); }
        std::string joined;
        for (const auto &line : chunk) joined += (joined.empty() ? "" : "\n") + line;
        auto answer = chunk.size() > 1 ? split_lines(generate(joined)) : std::vector<std::string>{};
        if (answer.size() == chunk.size()) { for (size_t i = 0; i < chunk.size(); ++i) fresh[chunk[i]] = answer[i]; continue; }
        for (const auto &line : chunk) {
            std::string text;
            for (const auto &part : split_lines(generate(line))) text += (text.empty() ? "" : " ") + part;
            fresh[line] = text;
        }
    }
    std::lock_guard lock(mutex_);
    auto &cache = load(key);
    for (auto &[k, v] : fresh) cache[k] = v;
    if (!fresh.empty()) save(key);
    std::vector<std::string> out;
    for (const auto &line : lines) { auto t = trim_text(line); out.push_back(t.empty() ? "" : cache[t]); }
    return out;
}
}
