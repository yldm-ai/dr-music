#include "application.h"
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
namespace dr {
using Microsoft::WRL::ComPtr;
Application::Application(fs::path data, fs::path assets, unsigned debug_port, std::vector<fs::path> imports, const std::string &shell)
    : library_(std::move(data)), assets_(std::move(assets)), debug_port_(debug_port), imports_(std::move(imports)), shell_(shell) {
    library_.load();
    worker_ = std::jthread([this](std::stop_token stop) {
        while (true) {
            std::function<void(std::stop_token)> task;
            { std::unique_lock lock(jobs_mutex_); ready_.wait(lock, stop, [this] { return !jobs_.empty(); });
              if (jobs_.empty()) { if (stop.stop_requested()) break; else continue; }
              task = std::move(jobs_.front()); jobs_.pop_front(); }
            task(stop);
        }
    });
}
Application::~Application() { closing_ = true; worker_.request_stop(); ready_.notify_all(); if (worker_.joinable()) worker_.join(); webview_.close(); }
void Application::enqueue(std::function<void(std::stop_token)> task) { { std::lock_guard lock(jobs_mutex_); jobs_.push_back(std::move(task)); } ready_.notify_one(); }
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
    const COMDLG_FILTERSPEC lyrics[]{{L"本地歌词", L"*.ttml;*.yrc;*.lrc;*.txt"}};
    if (kind == "files") dialog->SetFileTypes(1, audio);
    if (kind == "lyrics") dialog->SetFileTypes(1, lyrics);
    dialog->SetTitle(kind == "folder" ? L"选择音乐文件夹" : kind == "lyrics" ? L"选择本地歌词" : L"选择音乐文件");
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
            enqueue([this, id, inputs](std::stop_token stop) {
                try {
                    auto result = library_.import_paths(inputs, [this](Json p) { if (!closing_) window_.post([this, p] { if (!closing_) webview_.send({{"event", "library:progress"}, {"data", p}}); }); }, stop);
                    reply(id, result);
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
        if (method == "app:data") {
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(window_.handle(), L"open", library_.directory().c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) throw std::runtime_error("无法打开数据目录");
            reply(id, nullptr); return;
        }
        enqueue([this, id, method, params](std::stop_token) {
            try {
                if (method == "library:get") reply(id, library_.snapshot());
                else if (method == "library:update") {
                    auto updated = library_.update(params);
                    if (params.contains("settings") && params["settings"].contains("titleBar")) window_.post([this, s = updated["settings"]] { if (!closing_) window_.set_style(s.value("titleBar", "direct2d") == "direct2d"); });
                    reply(id, updated);
                } else if (method == "library:remove") reply(id, library_.remove(params.at("id").get<std::string>()));
                else if (method == "lyrics:get") reply(id, library_.lyrics(params.at("id").get<std::string>()));
                else if (method == "app:notices") reply(id, read_text(assets_.parent_path() / L"THIRD_PARTY_NOTICES.md"));
                else throw std::runtime_error("不支持的原生命令");
            } catch (const std::exception &e) { reject(id, e.what()); }
        });
    } catch (const std::exception &e) { reject(id, e.what()); }
}
int Application::run(HINSTANCE instance) {
    auto s = library_.snapshot()["settings"];
    if (!shell_.empty()) { s["titleBar"] = shell_; library_.update({{"settings", {{"titleBar", shell_}}}}); }
    if (!window_.create(instance, s.value("titleBar", "direct2d") == "direct2d", s.value("theme", "dark") == "dark")) throw std::runtime_error("无法创建 DR Music 窗口");
    window_.on_resize = [this] { webview_.resize(window_.content_bounds()); };
    window_.on_caption_tooltip = [this](const std::wstring &text, RECT button) {
        if (closing_) return;
        const double scale = 96.0 / GetDpiForWindow(window_.handle());
        webview_.send({{"event", "caption-tooltip"}, {"data", {{"text", utf8(text)}, {"left", button.left * scale}, {"width", (button.right - button.left) * scale}}}});
    };
    window_.on_close = [this] { closing_ = true; worker_.request_stop(); ready_.notify_all(); DestroyWindow(window_.handle()); };
    webview_.on_message = [this](const Json &message) { receive(message); };
    webview_.on_resource = [this](const std::wstring &uri, const std::string &range, const std::string &method) { return serve_media(library_, uri, range, method); };
    webview_.on_error = [this](const std::wstring &error) { MessageBoxW(window_.handle(), error.c_str(), L"DR Music", MB_OK | MB_ICONERROR); PostMessageW(window_.handle(), WM_CLOSE, 0, 0); };
    if (!imports_.empty()) enqueue([this](std::stop_token stop) { try { library_.import_paths(imports_, {}, stop); } catch (const std::exception &e) { window_.post([this, error = std::string(e.what())] { if (!closing_) MessageBoxW(window_.handle(), wide(error).c_str(), L"导入失败", MB_OK | MB_ICONERROR); }); } });
    webview_.resize(window_.content_bounds()); webview_.create(window_.handle(), assets_, library_.directory(), debug_port_);
    MSG message{}; int result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return result < 0 ? 1 : static_cast<int>(message.wParam);
}
}
