#include "application.h"
#include <shlobj.h>
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); if (FAILED(initialized)) return 1;
    int result = 1;
    try {
        SetCurrentProcessExplicitAppUserModelID(L"suxiaofan.dr-music");
        PWSTR roaming = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) throw std::runtime_error("无法找到用户数据目录");
        auto data = dr::fs::path(dr::take(roaming)) / L"DR Music";
        std::wstring executable(32768, L'\0'); executable.resize(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
        int argc = 0; auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::vector<dr::fs::path> imports; unsigned port = 0; std::string shell, translate_api; dr::fs::path online_lyrics_fixture;
        try {
            for (int i = 1; i < argc; ++i) {
                std::wstring arg = argv[i];
                if (arg == L"--data-dir" && i + 1 < argc) data = dr::fs::absolute(argv[++i]);
                else if (arg == L"--import" && i + 1 < argc) imports.emplace_back(argv[++i]);
                else if (arg == L"--debug-port" && i + 1 < argc) { port = static_cast<unsigned>(std::stoul(argv[++i])); if (port < 1024 || port > 65535) throw std::runtime_error("无效的调试端口"); }
                else if (arg == L"--shell" && i + 1 < argc) { shell = dr::utf8(argv[++i]); if (shell != "direct2d" && shell != "system" && shell != "webview2") throw std::runtime_error("--shell 仅支持 direct2d、webview2 或 system"); }
                // Tests serve the AMLL TTML DB layout from a local folder instead of the network.
                else if (arg == L"--online-lyrics-root" && i + 1 < argc) online_lyrics_fixture = dr::fs::absolute(argv[++i]);
                // Tests answer lyric translation from a stand-in on this computer, never the public API.
                else if (arg == L"--translate-api" && i + 1 < argc) {
                    translate_api = dr::utf8(argv[++i]);
                    const auto url = dr::parse_url(translate_api);
                    if (!url || url->https || url->host != "127.0.0.1") throw std::runtime_error("--translate-api 仅支持 http://127.0.0.1 上的测试服务");
                }
                else throw std::runtime_error("未知或不完整的命令行参数");
            }
        } catch (...) { LocalFree(argv); throw; }
        LocalFree(argv);
        auto assets = dr::fs::path(executable).parent_path() / L"web";
        if (!dr::fs::exists(assets / L"index.html")) throw std::runtime_error("找不到 web/index.html，请保持 web 文件夹与 dr-music.exe 位于同一目录");
        // Lock the data directory across processes to prevent lost library writes.
        dr::fs::create_directories(data);
        // A second launch brings the open window forward; an instance that is still shutting down gets a moment to finish.
        HANDLE lock = INVALID_HANDLE_VALUE;
        for (int attempt = 0;; ++attempt) {
            lock = CreateFileW((data / L"library.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (lock != INVALID_HANDLE_VALUE) break;
            if (GetLastError() != ERROR_SHARING_VIOLATION) throw std::runtime_error("无法打开音乐库目录，请检查目录是否可写");
            if (HWND open = FindWindowW(L"DRMusic.NativeShell", nullptr); open && IsWindowVisible(open)) {
                if (IsIconic(open)) ShowWindow(open, SW_RESTORE);
                SetForegroundWindow(open);
                CoUninitialize(); return 0;
            }
            if (attempt >= 50) throw std::runtime_error("此音乐库已由另一个 DR Music 实例打开（它可能仍在退出），请稍后再试");
            Sleep(200);
        }
        try { dr::Application application(data, assets, port, std::move(imports), shell, online_lyrics_fixture, translate_api); result = application.run(instance); }
        catch (...) { CloseHandle(lock); throw; }
        CloseHandle(lock);
    } catch (const std::exception &e) { MessageBoxW(nullptr, dr::wide(e.what()).c_str(), L"DR Music", MB_OK | MB_ICONERROR); }
    CoUninitialize(); return result;
}
