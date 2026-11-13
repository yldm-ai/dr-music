#include "local_translator.h"
#include "lyric_crypto.h"
#include <algorithm>
#include <cctype>
#include <sstream>
namespace dr {
namespace {
// Hy-MT2's chat tokens (U+FF5C "｜", U+2581 "▁"), as in CheemsAutoTranslate's Modelfile.Hy-MT2.
#define HY_BAR "\xEF\xBD\x9C"
#define HY_SP "\xE2\x96\x81"
constexpr const char *kBegin = "<" HY_BAR "hy_begin" HY_SP "of" HY_SP "sentence" HY_BAR ">";
constexpr const char *kUser = "<" HY_BAR "hy_User" HY_BAR ">";
constexpr const char *kAssistant = "<" HY_BAR "hy_Assistant" HY_BAR ">";
constexpr const char *kStop2 = "<" HY_BAR "hy_place" HY_SP "holder" HY_SP "no" HY_SP "2" HY_BAR ">";
constexpr const char *kStop8 = "<" HY_BAR "hy_place" HY_SP "holder" HY_SP "no" HY_SP "8" HY_BAR ">";
#undef HY_BAR
#undef HY_SP
HttpRequest request(const std::string &method, const std::string &url, std::string body = {}) {
    HttpRequest r{method, url, {{"Content-Type", "application/json"}}, std::move(body)};
    r.direct = true; r.limit = 4 * 1024 * 1024;
    return r;
}
}

LocalTranslator::LocalTranslator(fs::path cache_dir, HttpFetch fetch) : memory_(std::move(cache_dir)), fetch_(std::move(fetch)) {}

bool LocalTranslator::loopback(const std::string &host) { return host == "127.0.0.1" || host == "localhost"; }

bool LocalTranslator::valid_endpoint(const std::string &url) {
    const auto parsed = parse_url(url);
    return parsed && loopback(parsed->host) && (parsed->path == "/" || parsed->path.empty()) && url.size() <= 64;
}

bool LocalTranslator::valid_model(const std::string &name) {
    return !name.empty() && name.size() <= 200 && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == ':' || c == '/'; });
}

void LocalTranslator::set_options(Options options) {
    if (!valid_endpoint(options.endpoint)) options.endpoint = kDefaultEndpoint;
    while (options.endpoint.ends_with('/')) options.endpoint.pop_back();
    if (!valid_model(options.model)) options.model = kDefaultModel;
    std::lock_guard lock(mutex_); options_ = std::move(options);
}

LocalTranslator::Options LocalTranslator::options() const { std::lock_guard lock(mutex_); return options_; }

Json LocalTranslator::status() {
    const auto o = options();
    Json out = {{"endpoint", o.endpoint}, {"model", o.model}, {"reachable", false}, {"installed", false}, {"version", ""}, {"error", ""}};
    try {
        const auto version = fetch_(request("GET", o.endpoint + "/api/version"));
        if (version.status != 200) { out["error"] = "Ollama 返回 HTTP " + std::to_string(version.status); return out; }
        out["reachable"] = true;
        out["version"] = Json::parse(version.body, nullptr, false).value("version", "");
        const auto tags = Json::parse(fetch_(request("GET", o.endpoint + "/api/tags")).body, nullptr, false);
        if (tags.is_object() && tags.contains("models") && tags["models"].is_array())
            for (const auto &m : tags["models"]) {
                const auto name = m.value("name", m.value("model", ""));
                if (name == o.model || name == o.model + ":latest") { out["installed"] = true; out["size"] = m.value("size", 0.0); }
            }
    } catch (const std::exception &e) { out["error"] = e.what(); }
    return out;
}

void LocalTranslator::pull(const std::function<void(const Progress &)> &progress) {
    const auto o = options();
    auto r = request("POST", o.endpoint + "/api/pull", Json{{"model", o.model}, {"stream", true}}.dump());
    r.timeout_ms = 10 * 60 * 1000; // registry lookups and large layers can pause the stream
    std::string pending, error;
    r.on_data = [&](std::string_view chunk) {
        pending += chunk;
        for (size_t at; (at = pending.find('\n')) != std::string::npos;) {
            const auto line = Json::parse(pending.substr(0, at), nullptr, false); pending.erase(0, at + 1);
            if (!line.is_object()) continue;
            if (line.contains("error")) { error = line.value("error", "下载失败"); continue; }
            if (progress) progress({line.value("status", ""), line.value("completed", 0.0), line.value("total", 0.0)});
        }
    };
    const auto response = fetch_(r);
    if (response.status != 200) throw std::runtime_error("Ollama 返回 HTTP " + std::to_string(response.status));
    if (!error.empty()) throw std::runtime_error(error);
}

std::string LocalTranslator::import_gguf(const fs::path &file, const std::function<void(const Progress &)> &progress) {
    const auto o = options();
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || lower(path_text(file.extension())) != ".gguf") throw std::runtime_error("请选择 .gguf 模型文件");
    auto report = [&](const char *status, double done, double total) { if (progress) progress({status, done, total}); };
    const auto digest = "sha256:" + sha256_file_hex(file, [&](uint64_t done, uint64_t total) { report("正在校验模型文件", static_cast<double>(done), static_cast<double>(total)); });
    // Ollama keeps uploaded files as content-addressed blobs; skip the upload when it already has this one.
    if (fetch_(request("HEAD", o.endpoint + "/api/blobs/" + digest)).status != 200) {
        auto upload = request("POST", o.endpoint + "/api/blobs/" + digest);
        upload.headers = {{"Content-Type", "application/octet-stream"}};
        upload.body_file = file; upload.timeout_ms = 10 * 60 * 1000;
        upload.on_sent = [&](uint64_t done, uint64_t total) { report("正在导入 Ollama", static_cast<double>(done), static_cast<double>(total)); };
        const auto uploaded = fetch_(upload);
        if (uploaded.status != 200 && uploaded.status != 201) throw std::runtime_error("Ollama 拒绝了模型文件（HTTP " + std::to_string(uploaded.status) + "）");
    }
    const Json body = {
        {"model", kImportedModel}, {"files", {{path_text(file.filename()), digest}}}, {"stream", true},
        {"parameters", {{"temperature", 0.7}, {"top_p", 0.6}, {"top_k", 20}, {"repeat_penalty", 1.05}, {"num_ctx", 8192}, {"stop", {kStop2, kStop8}}}},
    };
    auto create = request("POST", o.endpoint + "/api/create", body.dump());
    create.timeout_ms = 10 * 60 * 1000;
    std::string pending, error;
    create.on_data = [&](std::string_view chunk) {
        pending += chunk;
        for (size_t at; (at = pending.find('\n')) != std::string::npos;) {
            const auto line = Json::parse(pending.substr(0, at), nullptr, false); pending.erase(0, at + 1);
            if (!line.is_object()) continue;
            if (line.contains("error")) error = line.value("error", "创建模型失败");
            else report("正在创建模型", 0, 0);
        }
    };
    const auto created = fetch_(create);
    if (!error.empty()) throw std::runtime_error("Ollama 创建模型失败：" + error);
    if (created.status != 200) throw std::runtime_error("Ollama 创建模型失败（HTTP " + std::to_string(created.status) + "）");
    return kImportedModel;
}

std::string LocalTranslator::prompt(const std::string &text, const std::string &source, const std::string &target) {
    // Tencent's Hy-MT prompts: the Chinese one whenever Chinese is involved, the English one otherwise.
    std::string body;
    if (source == "zh" || target == "zh")
        body = "将以下文本翻译为" + std::string(target == "zh" ? "中文" : "英语") + "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + text;
    else
        body = "Translate the following segment into " + std::string(target == "zh" ? "Chinese" : "English") + ", without additional explanation.\n\n" + text;
    return std::string(kBegin) + kUser + body + kAssistant;
}

std::string LocalTranslator::generate(const Options &o, const std::string &text, const std::string &source, const std::string &target) {
    const Json body = {
        {"model", o.model}, {"prompt", prompt(text, source, target)}, {"raw", true}, {"stream", false}, {"keep_alive", "10m"},
        {"options", {{"temperature", 0.7}, {"top_p", 0.6}, {"top_k", 20}, {"repeat_penalty", 1.05}, {"num_ctx", 8192},
                     {"num_predict", static_cast<int>(std::clamp<size_t>(text.size() * 3, 256, 4096))}, {"stop", {kStop2, kStop8}}}},
    };
    auto r = request("POST", o.endpoint + "/api/generate", body.dump());
    r.timeout_ms = 3 * 60 * 1000; // the first request also loads the model
    const auto response = fetch_(r);
    const auto json = Json::parse(response.body, nullptr, false);
    if (response.status == 404) throw std::runtime_error("本地还没有翻译模型，请先在设置中下载");
    if (response.status != 200 || !json.is_object()) throw std::runtime_error(json.is_object() && json.contains("error") ? json.value("error", "") : "Ollama 返回 HTTP " + std::to_string(response.status));
    auto out = json.value("response", "");
    for (auto stop : {kStop2, kStop8}) if (auto at = out.find(stop); at != std::string::npos) out.resize(at);
    return trim_text(out);
}

std::vector<std::string> LocalTranslator::translate(const std::vector<std::string> &lines, const std::string &source, const std::string &target) {
    const auto o = options();
    return memory_.translate(md5_hex("ollama\n" + o.model + "\n" + source + ">" + target), lines, [&](const std::string &text) { return generate(o, text, source, target); }, 12);
}
}
