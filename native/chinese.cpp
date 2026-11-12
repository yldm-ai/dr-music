#include "chinese.h"
#include <algorithm>
#include <SimpleConverter.hpp> // OpenCC's build interface exposes its src/ folder directly
namespace dr {
namespace {
// Traditional (any region, including Taiwan vocabulary) → simplified.
constexpr const char *to_simplified[] = {"t2s", "tw2s", "tw2sp", "hk2s"};
}

// Same code-point ranges as ../dr-dict/engine/src/chinese.cpp.
bool has_han(const std::string &text) {
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        char32_t cp = 0; int length = 1;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 0x6) cp = c & 0x1f, length = 2;
        else if ((c >> 4) == 0xe) cp = c & 0x0f, length = 3;
        else if ((c >> 3) == 0x1e) cp = c & 0x07, length = 4;
        if (i + length > text.size()) break;
        for (int k = 1; k < length; ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3f);
        i += length;
        if ((cp >= 0x3400 && cp <= 0x4dbf) || (cp >= 0x4e00 && cp <= 0x9fff) || (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0x20000 && cp <= 0x3134f)) return true;
    }
    return false;
}

ChineseText::ChineseText(fs::path config_dir) : dir_(std::move(config_dir)) {}
ChineseText::~ChineseText() = default;

bool ChineseText::available() const {
    std::error_code ec;
    return fs::is_regular_file(dir_ / L"t2s.json", ec);
}

std::string ChineseText::convert(const std::string &config, const std::string &text) {
    opencc::SimpleConverter *converter = nullptr;
    {
        std::lock_guard lock(mutex_);
        if (failed_.contains(config)) return text;
        auto &slot = converters_[config];
        if (!slot) {
            try {
                // OpenCC resolves the dictionaries a config names relative to the config file.
                slot = std::make_unique<opencc::SimpleConverter>(path_text(dir_ / wide(config + ".json")));
            } catch (const std::exception &) {
                converters_.erase(config); failed_.insert(config); return text;
            }
        }
        converter = slot.get();
    }
    // A loaded converter is immutable; Convert() is const and may run on several threads.
    try { return converter->Convert(text); } catch (const std::exception &) { return text; }
}

std::string ChineseText::simplify(const std::string &text) {
    return has_han(text) ? convert("t2s", text) : text;
}

std::vector<std::string> ChineseText::query_forms(const std::string &query) {
    std::vector<std::string> forms{query};
    if (!has_han(query) || query.size() > 512) return forms;
    for (auto *config : to_simplified) {
        auto value = convert(config, query);
        if (!value.empty() && std::find(forms.begin(), forms.end(), value) == forms.end()) forms.push_back(std::move(value));
    }
    return forms;
}
}
