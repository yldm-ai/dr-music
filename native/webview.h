#pragma once
#include "common.h"
#include "media.h"
#include <dcomp.h>
#include <windows.h>
#include <objbase.h>
#include <wrl.h>
#include <WebView2.h>
#include <functional>
namespace dr {
inline constexpr wchar_t app_index[] = L"https://app.dr-music.local/index.html";
class WebView {
public:
    void create(HWND window, const fs::path &web_root, const fs::path &data_root, unsigned debug_port);
    void resize(RECT bounds);
    void send(const Json &message);
    void close();
    void mouse(UINT message, WPARAM wParam, LPARAM lParam);
    void focus();
    bool cursor();
    void notify_move();
    std::function<void(const Json &)> on_message;
    std::function<MediaResponse(const std::wstring &, const std::string &, const std::string &)> on_resource;
    std::function<void(const std::wstring &)> on_error;
    std::function<void()> on_ready;
private:
    void configure(const fs::path &web_root);
    void fail(HRESULT error, const wchar_t *step);
    HWND window_ = nullptr;
    RECT bounds_{};
    bool debugging_ = false;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> composition_device_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> composition_target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> composition_root_;
    Microsoft::WRL::ComPtr<ICoreWebView2CompositionController> composition_controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> view_;
};
}
