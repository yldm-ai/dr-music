#include "niutrans.h"
#include "lyric_crypto.h"
#include <thread>
namespace dr {
namespace {
std::string error_message(const std::string &code, const std::string &message) {
    // The codes from the v2 documentation that a user can act on.
    if (code == "20001") return "小牛翻译鉴权失败，请检查 App ID 和 API Key";
    if (code == "13001") return "小牛翻译的字符额度不足，或该应用没有开通文本翻译";
    if (code == "10001") return "小牛翻译请求过于频繁，请稍后再试";
    if (code == "13007") return "小牛翻译不支持该语言";
    return "小牛翻译出错（" + code + "）" + (message.empty() ? "" : "：" + message);
}
}

NiuTrans::NiuTrans(fs::path cache_dir, HttpFetch fetch, std::function<long long()> clock)
    : memory_(std::move(cache_dir)), fetch_(std::move(fetch)), clock_(std::move(clock)) {
    if (!clock_) clock_ = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); };
}

void NiuTrans::set_credentials(std::string app_id, std::string apikey) {
    std::lock_guard lock(mutex_); app_id_ = std::move(app_id); apikey_ = std::move(apikey);
}
bool NiuTrans::configured() const { std::lock_guard lock(mutex_); return !app_id_.empty() && !apikey_.empty(); }
std::string NiuTrans::app_id() const { std::lock_guard lock(mutex_); return app_id_; }

std::string NiuTrans::auth_str(const std::map<std::string, std::string> &params, const std::string &apikey) {
    auto all = params; all["apikey"] = apikey; all.erase("authStr");
    std::string joined;
    for (const auto &[key, value] : all) if (!value.empty()) joined += (joined.empty() ? "" : "&") + key + "=" + value;
    return md5_hex(joined);
}

std::string NiuTrans::translate_text(const std::string &text, const std::string &source, const std::string &target) {
    std::string app_id, apikey;
    { std::lock_guard lock(mutex_); app_id = app_id_; apikey = apikey_;
      // The free tier allows a few requests per second; keep well under it.
      const auto wait = last_request_ + std::chrono::milliseconds(250) - std::chrono::steady_clock::now();
      if (wait > std::chrono::milliseconds(0)) std::this_thread::sleep_for(wait);
      last_request_ = std::chrono::steady_clock::now(); }
    if (app_id.empty() || apikey.empty()) throw std::runtime_error("请先在设置 → 工具中填写小牛翻译的 App ID 和 API Key");
    std::map<std::string, std::string> params = {{"from", source}, {"to", target}, {"srcText", text}, {"appId", app_id}, {"timestamp", std::to_string(clock_())}};
    params["authStr"] = auth_str(params, apikey);
    HttpRequest request{"POST", std::string("https://") + kHost + "/v2/text/translate", {{"Content-Type", "application/json"}}, Json(params).dump()};
    request.limit = 1024 * 1024;
    const auto response = fetch_(request);
    const auto json = Json::parse(response.body, nullptr, false);
    if (!json.is_object()) throw std::runtime_error("小牛翻译返回了无法识别的内容（HTTP " + std::to_string(response.status) + "）");
    auto field = [&](std::initializer_list<const char *> names) {
        for (auto name : names) if (json.contains(name)) { const auto &v = json[name]; return v.is_string() ? v.get<std::string>() : v.dump(); }
        return std::string();
    };
    if (auto code = field({"errorCode", "error_code"}); !code.empty()) throw std::runtime_error(error_message(code, field({"errorMsg", "error_msg"})));
    if (!json.contains("tgtText") || !json["tgtText"].is_string()) throw std::runtime_error("小牛翻译没有返回译文（HTTP " + std::to_string(response.status) + "）");
    return json["tgtText"].get<std::string>();
}

std::vector<std::string> NiuTrans::translate(const std::vector<std::string> &lines, const std::string &source, const std::string &target) {
    return memory_.translate(md5_hex("niutrans\n" + app_id() + "\n" + source + ">" + target), lines,
        [&](const std::string &text) { return translate_text(text, source, target); }, 40, 4500); // request limit 5000 characters
}
}
