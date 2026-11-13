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
    // Cloaking keeps native window behavior available to tests without
    // presenting their temporary shells on the desktop.
    bool create(HINSTANCE instance, bool direct2d, bool dark, bool cloaked = false);
    HWND handle() const { return hwnd_; }
    RECT content_bounds() const;
    void set_style(bool direct2d, bool webview = false);
    void set_appearance(bool dark, bool dimmed);
    void set_caption_layout(RECT caption, RECT minimize, RECT maximize, RECT close);
    void post(std::function<void()> action);
    std::function<void()> on_resize, on_close;
    std::function<void(const std::wstring &, RECT)> on_caption_tooltip;
    std::function<void(const std::wstring &)> on_caption_event;
    std::function<void(UINT, WPARAM, LPARAM)> on_mouse;
    std::function<bool()> on_cursor;
    std::function<void()> on_focus;
    std::function<void()> on_move;
    /** WebView caption mode: the pointer entered (true) or left (false) the window, caption included. */
    std::function<void(bool)> on_pointer;
    bool direct2d() const { return direct2d_; }
    bool web_caption() const { return web_caption_; }
private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK caption_procedure(HWND, UINT, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
    void paint(HWND surface);
    void layout_caption();
    void clear_web_hover();
    void redraw();
    void apply_frame();
    int scale(int dip) const;
    int caption_height() const;
    int caption_hit(POINT) const;
    void hover(int hit, bool nonclient);
    void caption_tip(int hit);
    void pointer_inside(bool inside);
    void check_pointer();
    RECT caption_button(int hit) const;
    HWND hwnd_ = nullptr;
    HWND caption_ = nullptr;
    bool direct2d_ = true, web_caption_ = false, dark_ = true, dimmed_ = false;
    bool caption_layout_ready_ = false, caption_hot_ = false, caption_pressed_ = false, web_mouse_inside_ = false, pointer_inside_ = false;
    RECT caption_rect_{}, minimize_rect_{}, maximize_rect_{}, close_rect_{};
    int hot_ = HTNOWHERE, pressed_ = HTNOWHERE, tip_ = HTNOWHERE;
    const std::wstring title_ = L"DR Music";
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> text_, icons_;
    std::mutex mutex_;
    std::vector<std::function<void()>> pending_;
};
}
