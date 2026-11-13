#include "application.h"
#include "fonts.h"
#include <wincrypt.h>
#include <winhttp.h>
#include <cmath>
#include <map>
#include <set>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
namespace dr {
using Microsoft::WRL::ComPtr;
Application::Application(fs::path data, fs::path assets, unsigned debug_port, std::vector<fs::path> imports, const std::string &shell, fs::path online_lyrics_fixture)
    : library_(std::move(data)), assets_(std::move(assets)), chinese_(assets_.parent_path() / L"opencc"),
      online_lyrics_(library_.directory() / L"online-lyrics",
          online_lyrics_fixture.empty() ? winhttp_fetch([this](const std::string &host) { return online_lyrics_.allowed_host(host); }, http_cancel_) : fixture_fetch(online_lyrics_fixture),
          [this](const std::string &text) { return chinese_.simplify(text); }),
      translator_(library_.directory() / L"translations", winhttp_fetch([](const std::string &host) { return LocalTranslator::loopback(host); }, http_cancel_)),
      niutrans_(library_.directory() / L"translations", winhttp_fetch([](const std::string &host) { return host == NiuTrans::kHost; }, http_cancel_)),
      debug_port_(debug_port), imports_(std::move(imports)), shell_(shell) {
    library_.load();
    apply_lyric_options();
    apply_translate_options();
    load_niutrans_key();
    worker_ = std::jthread([this](std::stop_token stop) {
        while (true) {
            std::function<void(std::stop_token)> task;
            { std::unique_lock lock(jobs_mutex_); ready_.wait(lock, stop, [this] { return !jobs_.empty() || !background_jobs_.empty(); });
              if (stop.stop_requested() && jobs_.empty()) break;
              auto &queue = jobs_.empty() ? background_jobs_ : jobs_;
              if (queue.empty()) continue;
              task = std::move(queue.front()); queue.pop_front(); }
            task(stop);
        }
    });
    transport_worker_ = std::jthread([this](std::stop_token stop) {
        while (true) {
            std::function<void()> task;
            { std::unique_lock lock(transport_mutex_); transport_ready_.wait(lock, stop, [this] { return !transport_jobs_.empty(); });
              if (stop.stop_requested() && transport_jobs_.empty()) break;
              if (transport_jobs_.empty()) continue;
              task = std::move(transport_jobs_.front()); transport_jobs_.pop_front(); }
            task();
        }
    });
    network_worker_ = std::jthread([this](std::stop_token stop) {
        while (true) {
            std::function<void()> task;
            { std::unique_lock lock(network_mutex_); network_ready_.wait(lock, stop, [this] { return !network_jobs_.empty(); });
              if (stop.stop_requested()) break; // pending lookups are abandoned on exit
              if (network_jobs_.empty()) continue;
              task = std::move(network_jobs_.front()); network_jobs_.pop_front(); }
            task();
        }
    });
    translate_worker_ = std::jthread([this](std::stop_token stop) {
        while (true) {
            std::function<void()> task;
            { std::unique_lock lock(translate_mutex_); translate_ready_.wait(lock, stop, [this] { return !translate_jobs_.empty(); });
              if (stop.stop_requested()) break;
              if (translate_jobs_.empty()) continue;
              task = std::move(translate_jobs_.front()); translate_jobs_.pop_front(); }
            task();
        }
    });
}
Application::~Application() {
    closing_ = true; http_cancel_->cancel(); // a translation or lyric download in flight must not keep the process (and library lock) alive
    worker_.request_stop(); ready_.notify_all(); if (worker_.joinable()) worker_.join();
    transport_worker_.request_stop(); transport_ready_.notify_all(); if (transport_worker_.joinable()) transport_worker_.join();
    network_worker_.request_stop(); network_ready_.notify_all(); if (network_worker_.joinable()) network_worker_.join();
    translate_worker_.request_stop(); translate_ready_.notify_all(); if (translate_worker_.joinable()) translate_worker_.join();
    system_media_.shutdown();
    playback_.set_listener({});
    playback_.command({{"action", "stop"}, {"request", UINT64_MAX}});
    try { library_.flush_metadata(); } catch (const std::exception &) { /* pending entries resume on the next launch */ }
    webview_.close();
}
void Application::enqueue(std::function<void(std::stop_token)> task, bool background) {
    { std::lock_guard lock(jobs_mutex_); (background ? background_jobs_ : jobs_).push_back(std::move(task)); } ready_.notify_one();
}
void Application::apply_lyric_options() {
    const auto settings = library_.settings();
    OnlineLyrics::Options options;
    if (settings.contains("lyricProviders") && settings["lyricProviders"].is_array()) {
        options.providers.clear();
        for (const auto &p : settings["lyricProviders"]) if (p.is_string()) options.providers.insert(p.get<std::string>());
    }
    options.amll_base = settings.value("amllMirror", "");
    options.apple_token = load_apple_token();
    online_lyrics_.set_options(std::move(options));
}
void Application::apply_translate_options() {
    const auto settings = library_.settings();
    translator_.set_options({settings.value("translateEndpoint", std::string(LocalTranslator::kDefaultEndpoint)), settings.value("translateModel", std::string(LocalTranslator::kDefaultModel))});
}
void Application::enqueue_translate(std::function<void()> task) {
    { std::lock_guard lock(translate_mutex_); translate_jobs_.push_back(std::move(task)); } translate_ready_.notify_one();
}
// Secrets (Apple Music token, 小牛翻译 key) are DPAPI-encrypted for this Windows user and never sent back to the page.
void Application::seal(const fs::path &file, const std::string &secret, const wchar_t *description) {
    std::error_code ec;
    if (secret.empty()) { fs::remove(file, ec); return; }
    DATA_BLOB input{static_cast<DWORD>(secret.size()), reinterpret_cast<BYTE *>(const_cast<char *>(secret.data()))}, output{};
    if (!CryptProtectData(&input, description, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) throw std::runtime_error("无法加密保存密钥");
    std::string sealed(reinterpret_cast<char *>(output.pbData), output.cbData); LocalFree(output.pbData);
    write_atomic(file, sealed);
}
std::string Application::unseal(const fs::path &file) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return {};
    try {
        auto sealed = read_bytes(file, 64 * 1024); // binary: read_text would reject it as invalid UTF-8
        DATA_BLOB input{static_cast<DWORD>(sealed.size()), reinterpret_cast<BYTE *>(sealed.data())}, output{};
        if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
        std::string secret(reinterpret_cast<char *>(output.pbData), output.cbData); SecureZeroMemory(output.pbData, output.cbData); LocalFree(output.pbData);
        return secret;
    } catch (const std::exception &) { return {}; }
}
fs::path Application::apple_token_file() const { return library_.directory() / L"apple-music-token.bin"; }
void Application::save_apple_token(const std::string &token) { seal(apple_token_file(), token, L"DR Music Apple Music token"); }
std::string Application::load_apple_token() const { return unseal(apple_token_file()); }
fs::path Application::niutrans_key_file() const { return library_.directory() / L"niutrans-key.bin"; }
void Application::load_niutrans_key() {
    const auto json = Json::parse(unseal(niutrans_key_file()), nullptr, false);
    niutrans_.set_credentials(json.is_object() ? json.value("appId", "") : "", json.is_object() ? json.value("apikey", "") : "");
}
std::vector<std::string> Application::translate_lines(const std::vector<std::string> &lines, const std::string &source, const std::string &target) {
    return library_.settings().value("translateProvider", "local") == "niutrans" ? niutrans_.translate(lines, source, target) : translator_.translate(lines, source, target);
}
void Application::enqueue_network(std::function<void()> task) {
    { std::lock_guard lock(network_mutex_); network_jobs_.push_back(std::move(task)); } network_ready_.notify_one();
}
void Application::enqueue_transport(std::function<void()> task) {
    { std::lock_guard lock(transport_mutex_); transport_jobs_.push_back(std::move(task)); } transport_ready_.notify_one();
}
void Application::schedule_metadata(Json candidates, size_t offset) {
    if (candidates.empty() || closing_) return;
    enqueue([this, candidates = std::move(candidates), offset](std::stop_token stop) mutable {
        Json updated = Json::array();
        const auto start = std::chrono::steady_clock::now();
        size_t count = 0;
        try {
            while (offset < candidates.size() && count < 32 && !stop.stop_requested()) {
                auto track = library_.enrich_metadata(candidates[offset++]);
                if (!track.is_null()) updated.push_back(std::move(track));
                ++count;
                if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(80)) break;
                std::lock_guard lock(jobs_mutex_);
                if (!jobs_.empty()) break;
            }
            const auto now = std::chrono::steady_clock::now();
            if (offset == candidates.size() || now - metadata_saved_ >= std::chrono::seconds(2)) {
                library_.flush_metadata(); metadata_saved_ = now;
            }
        } catch (const std::exception &e) {
            if (!closing_) window_.post([this, error = std::string(e.what())] {
                if (!closing_) webview_.send({{"event", "library:metadata-error"}, {"data", error}});
            });
        }
        if (!updated.empty() && !closing_) window_.post([this, updated = std::move(updated)] {
            if (!closing_) {
                if (!media_track_.is_null()) for (const auto &track : updated)
                    if (track["id"] == media_track_.value("id", "")) media_track_ = track;
                webview_.send({{"event", "library:metadata"}, {"data", updated}});
            }
        });
        if (offset < candidates.size() && !stop.stop_requested()) schedule_metadata(std::move(candidates), offset);
    }, true);
}
void Application::reply(const Json &id, Json result) { if (!closing_) window_.post([this, id, result = std::move(result)] { if (!closing_) webview_.send({{"id", id}, {"result", result}}); }); }
void Application::reject(const Json &id, const std::string &error) { if (!closing_) window_.post([this, id, error] { if (!closing_) webview_.send({{"id", id}, {"error", error}}); }); }
std::vector<fs::path> Application::choose_files(const std::string &kind) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) throw std::runtime_error("无法打开文件选择器");
    DWORD flags = FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
    if (kind != "lyrics") flags |= FOS_ALLOWMULTISELECT;
    if (kind == "folder") flags |= FOS_PICKFOLDERS;
    dialog->SetOptions(flags);
    const COMDLG_FILTERSPEC audio[]{{L"音频文件", L"*.mp3;*.flac;*.wav;*.m4a;*.aac;*.ogg;*.opus;*.webm;*.mp4"}};
    const COMDLG_FILTERSPEC lyrics[]{{L"歌词文件", L"*.ttml;*.yrc;*.lrc;*.txt"}};
    const COMDLG_FILTERSPEC model[]{{L"GGUF 模型文件", L"*.gguf"}};
    if (kind == "files") dialog->SetFileTypes(1, audio);
    if (kind == "lyrics") dialog->SetFileTypes(1, lyrics);
    if (kind == "model") { dialog->SetFileTypes(1, model); dialog->SetOptions(flags & ~FOS_ALLOWMULTISELECT); }
    dialog->SetTitle(kind == "folder" ? L"选择音乐文件夹" : kind == "lyrics" ? L"选择歌词文件" : kind == "model" ? L"选择下载好的 Hy-MT2 模型文件（.gguf）" : L"选择音乐文件");
    auto hr = dialog->Show(window_.handle());
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
    if (FAILED(hr)) throw std::runtime_error("文件选择器打开失败");
    ComPtr<IShellItemArray> items; dialog->GetResults(&items); DWORD count = 0; if (items) items->GetCount(&count);
    std::vector<fs::path> paths;
    for (DWORD i = 0; i < count; ++i) { ComPtr<IShellItem> item; items->GetItemAt(i, &item); LPWSTR raw = nullptr; if (item && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))) paths.emplace_back(take(raw)); }
    return paths;
}
void Application::receive(const Json &request) {
    if (closing_ || !request.is_object() || !request.contains("id") || !request["id"].is_number_unsigned() || !request.contains("method") || !request["method"].is_string()) return;
    const auto id = request["id"]; const auto method = request["method"].get<std::string>(); const auto params = request.value("params", Json::object());
    try {
        if (method == "playback:command") {
            if (!params.is_object() || !params.contains("request") || !params["request"].is_number_unsigned()) throw std::runtime_error("无效的播放请求");
            enqueue_transport([this, id, params] {
                try {
                    fs::path file;
                    if (params.at("action") == "play") file = library_.audio_path(params.at("id").get<std::string>());
                    reply(id, playback_.command(params, file));
                } catch (const std::exception &e) { reject(id, e.what()); }
            });
            return;
        }
        if (method == "window:caption-layout") {
            if (library_.settings().value("titleBar", "direct2d") != "webview2") throw std::runtime_error("当前标题栏不支持此操作");
            const auto dpr = params.value("dpr", 1.0);
            if (!std::isfinite(dpr) || dpr < 0.5 || dpr > 4.0) throw std::runtime_error("无效的标题栏缩放");
            auto rect = [&](const char *name) {
                const auto &value = params.at(name);
                const auto x = value.at("x").get<double>(), y = value.at("y").get<double>();
                const auto width = value.at("width").get<double>(), height = value.at("height").get<double>();
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) throw std::runtime_error("无效的标题栏区域");
                return RECT{static_cast<LONG>(std::lround(x * dpr)), static_cast<LONG>(std::lround(y * dpr)),
                    static_cast<LONG>(std::lround((x + width) * dpr)), static_cast<LONG>(std::lround((y + height) * dpr))};
            };
            window_.set_caption_layout(rect("caption"), rect("minimize"), rect("maximize"), rect("close"));
            reply(id, nullptr); return;
        }
        if (method == "window:command") {
            const auto command = params.at("command").get<std::string>();
            if (library_.settings().value("titleBar", "direct2d") != "webview2") throw std::runtime_error("当前标题栏不支持此操作");
            if (command == "minimize") PostMessageW(window_.handle(), WM_SYSCOMMAND, SC_MINIMIZE, 0);
            else if (command == "maximize") PostMessageW(window_.handle(), WM_SYSCOMMAND, IsZoomed(window_.handle()) ? SC_RESTORE : SC_MAXIMIZE, 0);
            else if (command == "close") PostMessageW(window_.handle(), WM_CLOSE, 0, 0);
            else throw std::runtime_error("无效的窗口操作");
            reply(id, nullptr); return;
        }
        if (method == "window:appearance") {
            window_.set_appearance(params.value("theme", "dark") == "dark", params.value("dimmed", false));
            reply(id, nullptr); return;
        }
        if (method == "library:import" || method == "library:rescan") {
            if (importing_) throw std::runtime_error("正在扫描，请稍候");
            std::vector<fs::path> inputs;
            if (method == "library:import") { const auto kind = params.at("kind").get<std::string>(); if (kind != "folder" && kind != "files") throw std::runtime_error("无效的导入类型"); inputs = choose_files(kind); if (inputs.empty()) { reply(id, nullptr); return; } }
            else {
                // Keep the snapshot alive: a range over snapshot()["folders"]
                // would refer to a destroyed temporary in C++20.
                const auto state = library_.snapshot();
                for (const auto &p : state["folders"]) inputs.push_back(from_text(p));
            }
            importing_ = true;
            enqueue([this, id, inputs, rescan = method == "library:rescan"](std::stop_token stop) {
                try {
                    auto result = library_.import_paths(inputs, [this](Json p) { if (!closing_) window_.post([this, p] { if (!closing_) webview_.send({{"event", "library:progress"}, {"data", p}}); }); }, stop, rescan);
                    auto pending = std::move(result["pending"]); result.erase("pending");
                    reply(id, std::move(result));
                    schedule_metadata(std::move(pending));
                } catch (const std::exception &e) { reject(id, e.what()); }
                window_.post([this] { importing_ = false; });
            });
            return;
        }
        if (method == "lyrics:attach") {
            auto track = params.at("id").get<std::string>(); (void)library_.track(track);
            auto files = choose_files("lyrics"); if (files.empty()) { reply(id, nullptr); return; }
            enqueue([this, id, track, file = files.front()](std::stop_token) { try { reply(id, library_.attach_lyrics(track, file)); } catch (const std::exception &e) { reject(id, e.what()); } }); return;
        }
        if (method == "track:reveal") {
            auto t = library_.track(params.at("id").get<std::string>()); auto file = from_text(t["path"]);
            PIDLIST_ABSOLUTE item = nullptr;
            if (FAILED(SHParseDisplayName(file.c_str(), nullptr, &item, 0, nullptr))) throw std::runtime_error("文件不存在，无法显示所在位置");
            auto hr = SHOpenFolderAndSelectItems(item, 0, nullptr, 0); CoTaskMemFree(item);
            if (FAILED(hr)) throw std::runtime_error("无法打开文件位置"); reply(id, nullptr); return;
        }
        if (method == "lyrics:online") {
            // Fixed lookup by library track: the page names a track, never a URL or query.
            const auto track_id = params.at("id").get<std::string>(); (void)library_.track(track_id);
            if (!library_.settings().value("onlineLyrics", true)) { reply(id, Json::array()); return; }
            const bool refresh = params.value("refresh", false);
            enqueue_network([this, id, track_id, refresh] {
                try {
                    Json results = Json::array();
                    for (const auto &r : online_lyrics_.search(library_.track(track_id), refresh))
                        results.push_back({{"provider", r.provider}, {"title", r.title}, {"artists", r.artists}, {"album", r.album},
                                           {"format", r.format}, {"text", r.text}, {"score", r.score}, {"reference", r.reference}, {"roman", r.roman}, {"romanFormat", r.roman_format}, {"translation", r.translation}, {"translationFormat", r.translation_format}});
                    reply(id, results);
                } catch (const std::exception &e) { reject(id, e.what()); }
            });
            return;
        }
        if (method == "lyrics:apple-token") {
            // The token is stored DPAPI-encrypted for this Windows user and never sent back to the page.
            const auto token = params.at("token").get<std::string>();
            if (token.size() > 4096 || token.find_first_of("\r\n\"'<>") != std::string::npos) throw std::runtime_error("无效的 Apple Music 令牌");
            save_apple_token(token); apply_lyric_options();
            reply(id, {{"appleToken", !token.empty()}}); return;
        }
        if (method == "lyrics:status") { reply(id, {{"appleToken", !load_apple_token().empty()}, {"providers", OnlineLyrics::provider_ids()}, {"amllDefault", OnlineLyrics::kAmllDefaultBase}}); return; }
        // 译: Hunyuan Hy-MT2 through a local Ollama (loopback only, see local_translator.h).
        if (method == "translate:status") {
            // The Windows proxy (e.g. Clash on 127.0.0.1:7890) is read so settings can tell the user how to
            // let Ollama use it: the Ollama service ignores it and only follows HTTPS_PROXY.
            std::string system_proxy;
            if (WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie{}; WinHttpGetIEProxyConfigForCurrentUser(&ie)) {
                if (ie.lpszProxy) {
                    auto value = utf8(ie.lpszProxy);
                    if (auto at = value.find("https="); at != std::string::npos) value = value.substr(at + 6);
                    else if (auto http = value.find("http="); http != std::string::npos) value = value.substr(http + 5);
                    value = value.substr(0, value.find_first_of("; "));
                    if (!value.empty() && value.find("://") == std::string::npos) value = "http://" + value;
                    system_proxy = value;
                }
                if (ie.lpszProxy) GlobalFree(ie.lpszProxy);
                if (ie.lpszProxyBypass) GlobalFree(ie.lpszProxyBypass);
                if (ie.lpszAutoConfigUrl) GlobalFree(ie.lpszAutoConfigUrl);
            }
            // The local model is only probed when asked (the settings page, or it is the chosen provider).
            const bool local = params.value("local", false) || library_.settings().value("translateProvider", "local") == "local";
            enqueue_translate([this, id, local, system_proxy] {
                reply(id, {{"provider", library_.settings().value("translateProvider", "local")}, {"local", local ? translator_.status() : Json()}, {"systemProxy", system_proxy},
                           {"niutrans", {{"configured", niutrans_.configured()}, {"appId", niutrans_.app_id()}}}});
            });
            return;
        }
        if (method == "translate:import") {
            // A .gguf downloaded in the browser, imported into the local Ollama (see LocalTranslator::import_gguf).
            auto files = choose_files("model");
            if (files.empty()) { reply(id, nullptr); return; }
            enqueue_translate([this, id, file = files.front()] {
                try {
                    auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                    const auto model = translator_.import_gguf(file, [&](const LocalTranslator::Progress &p) {
                        if (closing_) throw std::runtime_error("已取消");
                        const auto now = std::chrono::steady_clock::now();
                        if (now - last < std::chrono::milliseconds(200) && p.total > p.completed) return;
                        last = now;
                        window_.post([this, p] { if (!closing_) webview_.send({{"event", "translate:progress"}, {"data", {{"status", p.status}, {"completed", p.completed}, {"total", p.total}}}}); });
                    });
                    reply(id, {{"model", model}});
                } catch (const std::exception &e) { reject(id, e.what()); }
            });
            return;
        }
        if (method == "app:open-url") {
            // Fixed pages only (the page names one, never a URL): model downloads and service consoles.
            static const std::map<std::string, std::wstring> pages = {
                {"hy-mt2", L"https://huggingface.co/tencent/Hy-MT2-1.8B-GGUF/tree/main"},
                {"hy-mt2-mirror", L"https://hf-mirror.com/tencent/Hy-MT2-1.8B-GGUF/tree/main"},
                {"ollama", L"https://ollama.com/download/windows"},
                {"niutrans", L"https://niutrans.com/cloud/api/list"},
            };
            const auto page = pages.find(params.at("page").get<std::string>());
            if (page == pages.end()) throw std::runtime_error("未知的页面");
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(window_.handle(), L"open", page->second.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) throw std::runtime_error("无法打开浏览器");
            reply(id, nullptr); return;
        }
        if (method == "translate:niutrans-key") {
            const auto app_id = params.at("appId").get<std::string>(), apikey = params.at("apikey").get<std::string>();
            auto plain = [](const std::string &s) { return s.size() <= 128 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; }); };
            if (!plain(app_id) || !plain(apikey) || app_id.empty() != apikey.empty()) throw std::runtime_error("App ID 或 API Key 格式不正确");
            seal(niutrans_key_file(), app_id.empty() ? "" : Json{{"appId", app_id}, {"apikey", apikey}}.dump(), L"DR Music NiuTrans key");
            load_niutrans_key();
            reply(id, {{"configured", niutrans_.configured()}, {"appId", niutrans_.app_id()}}); return;
        }
        if (method == "translate:test") {
            // A fixed sample through the chosen provider, so settings can confirm the key / model works.
            enqueue_translate([this, id] {
                try { reply(id, translate_lines({"故事的小黄花", "从出生那年就飘着"}, "zh", "en")); } catch (const std::exception &e) { reject(id, e.what()); }
            });
            return;
        }
        if (method == "translate:pull") {
            enqueue_translate([this, id] {
                try {
                    auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                    translator_.pull([&](const LocalTranslator::Progress &p) {
                        if (closing_) throw std::runtime_error("已取消");
                        const auto now = std::chrono::steady_clock::now();
                        if (now - last < std::chrono::milliseconds(200) && p.total > p.completed) return; // throttle progress events
                        last = now;
                        window_.post([this, p] { if (!closing_) webview_.send({{"event", "translate:progress"}, {"data", {{"status", p.status}, {"completed", p.completed}, {"total", p.total}}}}); });
                    });
                    reply(id, translator_.status());
                } catch (const std::exception &e) { reject(id, e.what()); }
            });
            return;
        }
        if (method == "translate:lines") {
            const auto &lines = params.at("lines");
            const auto source = params.value("source", "zh"), target = params.at("target").get<std::string>();
            static const std::set<std::string> languages = {"zh", "en", "ja", "ko"};
            if (!lines.is_array() || lines.size() > 2000 || !languages.contains(source) || (target != "zh" && target != "en")) throw std::runtime_error("无效的翻译请求");
            std::vector<std::string> texts;
            for (const auto &line : lines) {
                if (!line.is_string() || line.get_ref<const std::string &>().size() > 2000) throw std::runtime_error("无效的翻译请求");
                texts.push_back(line.get<std::string>());
            }
            enqueue_translate([this, id, texts = std::move(texts), source, target] {
                try { reply(id, translate_lines(texts, source, target)); } catch (const std::exception &e) { reject(id, e.what()); }
            });
            return;
        }
        if (method == "app:data") {
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(window_.handle(), L"open", library_.directory().c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) throw std::runtime_error("无法打开数据目录");
            reply(id, nullptr); return;
        }
        enqueue([this, id, method, params](std::stop_token) {
            try {
                if (method == "library:get") reply(id, library_.snapshot());
                else if (method == "library:update") {
                    auto updated = library_.update(params);
                    if (params.contains("settings") && (params["settings"].contains("lyricProviders") || params["settings"].contains("amllMirror"))) apply_lyric_options();
                    if (params.contains("settings") && (params["settings"].contains("translateEndpoint") || params["settings"].contains("translateModel") || params["settings"].contains("translateProvider"))) {
                        apply_translate_options();
                        // Settings re-check the provider only after the host uses the new values.
                        window_.post([this] { if (!closing_) webview_.send({{"event", "translate:changed"}}); });
                    }
                    if (params.contains("settings") && params["settings"].contains("titleBar")) window_.post([this, s = updated["settings"]] { if (!closing_) window_.set_style(s.value("titleBar", "direct2d") == "direct2d", s.value("titleBar", "direct2d") == "webview2"); });
                    reply(id, updated);
                } else if (method == "library:remove" || method == "library:remove-folder") {
                    auto result = method == "library:remove" ? library_.remove(params.at("id").get<std::string>()) : library_.remove_folder(params.at("folder").get<std::string>());
                    auto state = playback_.command({{"action", "state"}, {"request", 0ULL}});
                    const auto current = state.value("id", "");
                    if (!current.empty() && std::none_of(result["tracks"].begin(), result["tracks"].end(), [&](const Json &track) { return track["id"] == current; }))
                        playback_.command({{"action", "stop"}, {"request", state["request"]}});
                    reply(id, result);
                }
                else if (method == "lyrics:get") reply(id, library_.lyrics(params.at("id").get<std::string>()));
                else if (method == "lyrics:preference") reply(id, library_.set_lyric_preference(params.at("id").get<std::string>(), params.at("source").get<std::string>()));
                else if (method == "app:notices") reply(id, read_text(assets_.parent_path() / L"THIRD_PARTY_NOTICES.md"));
                else if (method == "chinese:status") reply(id, {{"available", chinese_.available()}});
                else if (method == "chinese:simplify") {
                    // Folds track text for simplified/traditional search; bounded so a page cannot pin the worker.
                    const auto &texts = params.at("texts");
                    if (!texts.is_array() || texts.size() > 100000) throw std::runtime_error("无效的简繁转换请求");
                    Json result = Json::array();
                    for (const auto &text : texts) {
                        if (!text.is_string() || text.get_ref<const std::string &>().size() > 4096) throw std::runtime_error("无效的简繁转换请求");
                        result.push_back(chinese_.simplify(text.get<std::string>()));
                    }
                    reply(id, result);
                } else if (method == "chinese:convert") {
                    // 原 / 简 / 繁 for the lyric being shown: one lyric file (TTML, LRC, …) per call.
                    const auto text = params.at("text").get<std::string>(), script = params.at("script").get<std::string>(), variant = params.value("variant", "t");
                    if (text.size() > 8 * 1024 * 1024 || (script != "s" && script != "t") || (variant != "t" && variant != "tw" && variant != "twp" && variant != "hk"))
                        throw std::runtime_error("无效的简繁转换请求");
                    reply(id, chinese_.convert_script(text, script, variant));
                } else if (method == "chinese:query") {
                    const auto query = params.at("query").get<std::string>();
                    if (query.size() > 512) throw std::runtime_error("搜索内容过长");
                    reply(id, chinese_.query_forms(query));
                }
                else if (method == "app:fonts") { Json fonts = Json::array(); for (const auto &font : system_font_families()) fonts.push_back({{"family", font.family}, {"name", font.display}}); reply(id, fonts); }
                else throw std::runtime_error("不支持的原生命令");
            } catch (const std::exception &e) { reject(id, e.what()); }
        });
    } catch (const std::exception &e) { reject(id, e.what()); }
}
int Application::run(HINSTANCE instance) {
    auto s = library_.settings();
    if (!shell_.empty()) { s["titleBar"] = shell_; library_.update({{"settings", {{"titleBar", shell_}}}}); }
    if (!window_.create(instance, s.value("titleBar", "direct2d") == "direct2d", s.value("theme", "dark") == "dark")) throw std::runtime_error("无法创建 DR Music 窗口");
    playback_.set_listener([this](Json state) {
        if (!closing_) window_.post([this, state = std::move(state)] {
            if (closing_) return;
            const auto id = state.value("id", "");
            if (id.empty()) media_track_ = Json::object();
            if (!id.empty() && (media_track_.is_null() || media_track_.value("id", "") != id)) {
                try { media_track_ = library_.track(id); } catch (const std::exception &) { media_track_ = Json::object(); }
            }
            system_media_.update(state, media_track_.is_null() ? Json::object() : media_track_);
            webview_.send({{"event", "playback:state"}, {"data", state}});
        });
    });
    system_media_.initialize(window_.handle(), [this](Json action) {
        if (!closing_) window_.post([this, action = std::move(action)] { if (!closing_) webview_.send({{"event", "playback:action"}, {"data", action}}); });
    });
    window_.set_style(s.value("titleBar", "direct2d") == "direct2d", s.value("titleBar", "direct2d") == "webview2");
    window_.on_resize = [this] { webview_.resize(window_.content_bounds()); webview_.send({{"event", "window-state"}, {"data", {{"maximized", IsZoomed(window_.handle()) != FALSE}}}}); };
    window_.on_caption_tooltip = [this](const std::wstring &text, RECT button) {
        if (closing_) return;
        const double scale = 96.0 / GetDpiForWindow(window_.handle());
        webview_.send({{"event", "caption-tooltip"}, {"data", {{"text", utf8(text)}, {"left", button.left * scale}, {"width", (button.right - button.left) * scale}}}});
    };
    window_.on_caption_event = [this](const std::wstring &event) { if (!closing_) webview_.send({{"event", "caption-button"}, {"data", {{"event", utf8(event)}}}}); };
    window_.on_mouse = [this](UINT message, WPARAM w, LPARAM l) { webview_.mouse(message, w, l); };
    window_.on_cursor = [this] { return webview_.cursor(); };
    window_.on_focus = [this] { webview_.focus(); };
    window_.on_move = [this] { webview_.notify_move(); };
    window_.on_close = [this] { closing_ = true; http_cancel_->cancel(); worker_.request_stop(); ready_.notify_all(); DestroyWindow(window_.handle()); };
    webview_.on_message = [this](const Json &message) { receive(message); };
    webview_.on_resource = [this](const std::wstring &uri, const std::string &range, const std::string &method) { return serve_media(library_, uri, range, method); };
    webview_.on_error = [this](const std::wstring &error) { MessageBoxW(window_.handle(), error.c_str(), L"DR Music", MB_OK | MB_ICONERROR); PostMessageW(window_.handle(), WM_CLOSE, 0, 0); };
    if (!imports_.empty()) enqueue([this](std::stop_token stop) {
        try { auto result = library_.import_paths(imports_, {}, stop); schedule_metadata(std::move(result["pending"])); }
        catch (const std::exception &e) { window_.post([this, error = std::string(e.what())] { if (!closing_) MessageBoxW(window_.handle(), wide(error).c_str(), L"导入失败", MB_OK | MB_ICONERROR); }); }
    });
    schedule_metadata(library_.pending_metadata());
    webview_.resize(window_.content_bounds()); webview_.create(window_.handle(), assets_, library_.directory(), debug_port_);
    MSG message{}; int result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return result < 0 ? 1 : static_cast<int>(message.wParam);
}
}
