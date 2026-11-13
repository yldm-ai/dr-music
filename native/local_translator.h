#pragma once
#include "http.h"
#include "translation_memory.h"
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
namespace dr {
// Lyric translation with Tencent's Hunyuan translation model (Hy-MT2 1.8B, GGUF) served by a local
// Ollama, after ../RefProjects/CheemsAutoTranslate (docs/models/Modelfile.Hy-MT2, TranslationService):
// the model's own chat tokens around a single user turn, Tencent's recommended sampling. The prompt
// is built here and sent in Ollama's raw mode, so a plain `ollama pull` of the GGUF is enough.
// Only loopback hosts are ever contacted; lyrics never leave the computer.
class LocalTranslator {
public:
    static constexpr const char *kDefaultEndpoint = "http://127.0.0.1:11434";
    static constexpr const char *kDefaultModel = "hf.co/tencent/Hy-MT2-1.8B-GGUF:Q4_K_M";
    /** The name a model file imported from disk gets in Ollama. */
    static constexpr const char *kImportedModel = "hy-mt2-translate";
    struct Options { std::string endpoint = kDefaultEndpoint, model = kDefaultModel; };
    struct Progress { std::string status; double completed = 0, total = 0; };

    LocalTranslator(fs::path cache_dir, HttpFetch fetch);
    void set_options(Options options);
    Options options() const;
    /** http(s)://localhost or 127.0.0.1 with an optional port, nothing else. */
    static bool valid_endpoint(const std::string &url);
    static bool valid_model(const std::string &name);
    static bool loopback(const std::string &host);
    /** {reachable, version, installed, model, endpoint, error}. */
    Json status();
    /** Asks Ollama to download the model, reporting its progress lines. */
    void pull(const std::function<void(const Progress &)> &progress);
    /**
     * Imports a downloaded .gguf into Ollama (for when `ollama pull` cannot reach the registry):
     * hashes it, uploads it as a blob (skipped if Ollama already has it) and creates kImportedModel
     * from it. Returns the model name.
     */
    std::string import_gguf(const fs::path &file, const std::function<void(const Progress &)> &progress);
    /** One translation per input line ("" stays ""); `source` / `target` are zh, en, ja, ko. */
    std::vector<std::string> translate(const std::vector<std::string> &lines, const std::string &source, const std::string &target);
    /** The raw prompt for one request (exposed for tests). */
    static std::string prompt(const std::string &text, const std::string &source, const std::string &target);
private:
    std::string generate(const Options &options, const std::string &text, const std::string &source, const std::string &target);
    TranslationMemory memory_;
    HttpFetch fetch_;
    mutable std::mutex mutex_;
    Options options_;
};
}
