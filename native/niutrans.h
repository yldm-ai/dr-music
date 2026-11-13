#pragma once
#include "http.h"
#include "translation_memory.h"
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
namespace dr {
/**
 * 小牛翻译 (NiuTrans) text API v2, https://niutrans.com/documents/contents/transapi_text_v2 :
 * POST https://api.niutrans.com/v2/text/translate with {from, to, srcText, appId, timestamp, authStr},
 * authStr = MD5 of "key=value" pairs (apikey included, empty values and authStr left out) joined
 * with "&" in ASCII key order. The user's own App ID / API Key come from the NiuTrans console.
 */
class NiuTrans {
public:
    static constexpr const char *kHost = "api.niutrans.com";
    NiuTrans(fs::path cache_dir, HttpFetch fetch, std::function<long long()> clock = {});
    void set_credentials(std::string app_id, std::string apikey);
    bool configured() const;
    std::string app_id() const;
    static std::string auth_str(const std::map<std::string, std::string> &params, const std::string &apikey);
    /** One request (several lines may be joined with "\n"). */
    std::string translate_text(const std::string &text, const std::string &source, const std::string &target);
    std::vector<std::string> translate(const std::vector<std::string> &lines, const std::string &source, const std::string &target);
private:
    TranslationMemory memory_;
    HttpFetch fetch_;
    std::function<long long()> clock_;
    mutable std::mutex mutex_;
    std::string app_id_, apikey_;
    std::chrono::steady_clock::time_point last_request_{};
};
}
