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
        std::vector<dr::fs::path> imports; unsigned port = 0; std::string shell;
        try {
            for (int i = 1; i < argc; ++i) {
                std::wstring arg = argv[i];
                if (arg == L"--data-dir" && i + 1 < argc) data = dr::fs::absolute(argv[++i]);
                else if (arg == L"--import" && i + 1 < argc) imports.emplace_back(argv[++i]);
                else if (arg == L"--debug-port" && i + 1 < argc) { port = static_cast<unsigned>(std::stoul(argv[++i])); if (port < 1024 || port > 65535) throw std::runtime_error("无效的调试端口"); }
                else if (arg == L"--shell" && i + 1 < argc) { shell = dr::utf8(argv[++i]); if (shell != "direct2d" && shell != "system") throw std::runtime_error("--shell 仅支持 direct2d 或 system"); }
                else throw std::runtime_error("未知或不完整的命令行参数");
            }
        } catch (...) { LocalFree(argv); throw; }
        LocalFree(argv);
        auto assets = dr::fs::path(executable).parent_path() / L"web";
        if (!dr::fs::exists(assets / L"index.html")) throw std::runtime_error("找不到 web/index.html，请保持 web 文件夹与 dr-music.exe 位于同一目录");
        // Lock the data directory across processes to prevent lost library writes.
        dr::fs::create_directories(data);
        auto lock = CreateFileW((data / L"library.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock == INVALID_HANDLE_VALUE) throw std::runtime_error("此音乐库已由另一个 DR Music 实例打开，或目录不可写");
        try { dr::Application application(data, assets, port, std::move(imports), shell); result = application.run(instance); }
        catch (...) { CloseHandle(lock); throw; }
        CloseHandle(lock);
    } catch (const std::exception &e) { MessageBoxW(nullptr, dr::wide(e.what()).c_str(), L"DR Music", MB_OK | MB_ICONERROR); }
    CoUninitialize(); return result;
}
