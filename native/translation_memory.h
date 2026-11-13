#pragma once
#include "common.h"
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
namespace dr {
/**
 * Line-by-line lyric translation shared by every translation provider: blank lines stay blank,
 * repeated lines are translated once, a few lines go out per request (context and speed) and a
 * request whose answer does not keep one line per line is retried line by line. Results are kept
 * in `<dir>/<key>.json`, so a lyric is translated only once per provider / model / language pair.
 */
class TranslationMemory {
public:
    using Generate = std::function<std::string(const std::string &text)>;
    explicit TranslationMemory(fs::path dir);
    std::vector<std::string> translate(const std::string &key, const std::vector<std::string> &lines, const Generate &generate, size_t chunk_lines, size_t chunk_chars = 4000);
private:
    std::map<std::string, std::string> &load(const std::string &key);
    void save(const std::string &key);
    fs::path dir_;
    std::mutex mutex_;
    std::map<std::string, std::map<std::string, std::string>> caches_;
};
std::string trim_text(std::string text);
}
