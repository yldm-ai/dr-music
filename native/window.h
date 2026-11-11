#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
namespace dr {
// Based on the shell boundaries and caption hit-test approach in
// ../dr-dict/ui/{include/dr/window.h,src/window.cpp}; music-specific host.
class Window {
public:
    bool create(HINSTANCE instance, bool direct2d, bool dark);
    HWND handle() const { return hwnd_; }
    RECT content_bounds() const;
    void set_style(bool direct2d);
    void set_appearance(bool dark, bool dimmed, const std::wstring &title);
    void post(std::function<void()> action);
    std::function<void()> on_resize, on_close;
    bool direct2d() const { return direct2d_; }
private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK caption_procedure(HWND, UINT, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
    void paint(HWND surface);
    void layout_caption();
    void redraw();
    void apply_frame();
    int scale(int dip) const;
    int caption_height() const;
    int caption_hit(POINT) const;
    void hover(int hit, bool nonclient);
    HWND hwnd_ = nullptr;
    HWND caption_ = nullptr;
    bool direct2d_ = true, dark_ = true, dimmed_ = false;
    int hot_ = HTNOWHERE, pressed_ = HTNOWHERE;
    std::wstring title_ = L"DR Music";
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_;
    std::mutex mutex_;
    std::vector<std::function<void()>> pending_;
};
}
