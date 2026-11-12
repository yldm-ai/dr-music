#pragma once
#include "common.h"
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
namespace opencc { class SimpleConverter; }
namespace dr {
// Simplified / traditional folding for library search, built on OpenCC.
//
// Adapted from ChineseVariants in ../dr-dict/engine/src/chinese.cpp (itself after GoldenDict-ng's
// CharacterConversionDictionary). dr-dict expands a dictionary query into several written forms;
// a music library is filtered locally instead, so both sides are folded to simplified characters:
// track text through OpenCC's standard t2s, and the query through every traditional→simplified
// chain (t2s, tw2s, tw2sp, hk2s) so traditional input of any region matches.
class ChineseText {
public:
    /** `config_dir` holds OpenCC's *.json configs and the *.ocd2 dictionaries they name. */
    explicit ChineseText(fs::path config_dir);
    ~ChineseText();
    ChineseText(const ChineseText &) = delete;
    ChineseText &operator=(const ChineseText &) = delete;
    /** Whether OpenCC's data shipped with the app. */
    bool available() const;
    /** `text` in simplified characters; text without Han characters is returned unchanged. */
    std::string simplify(const std::string &text);
    /** Distinct simplified writings of a search query, starting with the query itself. */
    std::vector<std::string> query_forms(const std::string &query);
private:
    std::string convert(const std::string &config, const std::string &text);
    fs::path dir_;
    std::mutex mutex_;
    std::map<std::string, std::unique_ptr<opencc::SimpleConverter>> converters_;
    std::set<std::string> failed_;
};
/** Whether UTF-8 `text` contains a CJK unified ideograph (including extensions and compatibility forms). */
bool has_han(const std::string &text);
}
