#pragma once
#include "http.h"
#include "translation_memory.h"
#include <array>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
namespace dr {
/**
 * 哔哩哔哩 Index-Translate, the free public API of https://github.com/bilibili/Index-Translate
 * (inference/llm/call_api.py): an OpenAI-style POST https://index-translate.bilibili.com/v1/chat/completions
 * without a key, one user message with the project's own prompt, temperature 0 and thinking off
 * (chat_template_kwargs.enable_thinking = false). The reply is choices[0].message.content, with any
 * <think> block dropped.
 */
class IndexTranslate {
public:
    static constexpr const char *kHost = "index-translate.bilibili.com";
    static constexpr const char *kDefaultBase = "https://index-translate.bilibili.com/v1";
    static constexpr std::array<const char *, 3> kModels = {"Index-Translate-2B", "Index-Translate-9B", "Index-Translate-35B-A3B"};
    static constexpr const char *kDefaultModel = "Index-Translate-2B";

    /** `base` is the API root; only tests point it elsewhere (a loopback stand-in). */
    IndexTranslate(fs::path cache_dir, HttpFetch fetch, std::string base = kDefaultBase);
    static bool valid_model(const std::string &name);
    void set_model(std::string model);
    std::string model() const;
    /** call_api.py's build_prompt for a plain translation; `source` / `target` are zh, en, ja, ko. */
    static std::string prompt(const std::string &text, const std::string &source, const std::string &target);
    /** Drops a leading chain of thought (call_api.py's strip_think). */
    static std::string strip_think(std::string text);
    /** One request (several lines may be joined with "\n"). */
    std::string translate_text(const std::string &text, const std::string &source, const std::string &target);
    /** One translation per input line ("" stays ""), cached per model and language pair. */
    std::vector<std::string> translate(const std::vector<std::string> &lines, const std::string &source, const std::string &target);
private:
    TranslationMemory memory_;
    HttpFetch fetch_;
    std::string base_;
    mutable std::mutex mutex_;
    std::string model_ = kDefaultModel;
    std::chrono::steady_clock::time_point last_request_{};
};
}
