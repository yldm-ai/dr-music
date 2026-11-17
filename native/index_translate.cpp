#include "index_translate.h"
#include "lyric_crypto.h"
#include <algorithm>
#include <thread>
namespace dr {
namespace {
const char *language_name(const std::string &code) {
    if (code == "zh") return "中文";
    if (code == "en") return "英语";
    if (code == "ja") return "日语";
    if (code == "ko") return "韩语";
    return nullptr;
}
std::string error_text(const Json &json) {
    if (!json.is_object()) return {};
    if (json.contains("error")) {
        const auto &e = json["error"];
        if (e.is_string()) return e.get<std::string>();
        if (e.is_object() && e.contains("message") && e["message"].is_string()) return e["message"].get<std::string>();
    }
    if (json.contains("detail") && json["detail"].is_string()) return json["detail"].get<std::string>();
    if (json.contains("message") && json["message"].is_string()) return json["message"].get<std::string>();
    return {};
}
}

IndexTranslate::IndexTranslate(fs::path cache_dir, HttpFetch fetch, std::string base)
    : memory_(std::move(cache_dir)), fetch_(std::move(fetch)), base_(std::move(base)) {
    while (base_.ends_with('/')) base_.pop_back();
}

bool IndexTranslate::valid_model(const std::string &name) {
    return std::find(kModels.begin(), kModels.end(), name) != kModels.end();
}
void IndexTranslate::set_model(std::string model) {
    if (!valid_model(model)) model = kDefaultModel;
    std::lock_guard lock(mutex_); model_ = std::move(model);
}
std::string IndexTranslate::model() const { std::lock_guard lock(mutex_); return model_; }

std::string IndexTranslate::prompt(const std::string &text, const std::string &source, const std::string &target) {
    const auto to = language_name(target);
    if (!to) throw std::runtime_error("不支持的目标语言");
    const auto from = language_name(source);
    return std::string("请将以下") + (from ? from : "") + "文本翻译为" + to + "，直接输出翻译结果，不要进行任何解释。\n\n" + trim_text(text);
}

std::string IndexTranslate::strip_think(std::string text) {
    if (const auto end = text.find("</think>"); end != std::string::npos) text.erase(0, end + 8);
    text = trim_text(std::move(text));
    if (text.starts_with("<think>")) text = trim_text(text.substr(7));
    return text;
}

std::string IndexTranslate::translate_text(const std::string &text, const std::string &source, const std::string &target) {
    std::string model;
    { std::lock_guard lock(mutex_); model = model_;
      // A free public service: keep requests a little apart.
      const auto wait = last_request_ + std::chrono::milliseconds(200) - std::chrono::steady_clock::now();
      if (wait > std::chrono::milliseconds(0)) std::this_thread::sleep_for(wait);
      last_request_ = std::chrono::steady_clock::now(); }
    const Json body = {
        {"model", model}, {"messages", Json::array({{{"role", "user"}, {"content", prompt(text, source, target)}}})},
        {"temperature", 0.0}, {"max_tokens", static_cast<int>(std::clamp<size_t>(text.size() * 3, 256, 4096))}, {"stream", false},
        {"chat_template_kwargs", {{"enable_thinking", false}}},
    };
    HttpRequest request{"POST", base_ + "/chat/completions", {{"Content-Type", "application/json"}}, body.dump()};
    request.limit = 1024 * 1024; request.timeout_ms = 120000; // the larger models can take a while
    if (const auto url = parse_url(base_); url && (url->host == "127.0.0.1" || url->host == "localhost")) request.direct = true;
    const auto response = fetch_(request);
    const auto json = Json::parse(response.body, nullptr, false);
    if (response.status == 429) throw std::runtime_error("哔哩哔哩翻译请求过于频繁，请稍后再试");
    if (response.status != 200) {
        const auto detail = error_text(json);
        throw std::runtime_error("哔哩哔哩翻译出错（HTTP " + std::to_string(response.status) + "）" + (detail.empty() ? "" : "：" + detail));
    }
    if (!json.is_object() || !json.contains("choices") || !json["choices"].is_array() || json["choices"].empty())
        throw std::runtime_error("哔哩哔哩翻译返回了无法识别的内容");
    const auto &message = json["choices"][0].value("message", Json::object());
    if (!message.is_object() || !message.contains("content") || !message["content"].is_string()) throw std::runtime_error("哔哩哔哩翻译没有返回译文");
    return strip_think(message["content"].get<std::string>());
}

std::vector<std::string> IndexTranslate::translate(const std::vector<std::string> &lines, const std::string &source, const std::string &target) {
    return memory_.translate(md5_hex("index-translate\n" + model() + "\n" + source + ">" + target), lines,
        [&](const std::string &text) { return translate_text(text, source, target); }, 16, 1500);
}
}
