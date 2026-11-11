#include "window.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
namespace dr {
namespace { constexpr UINT dispatch = WM_APP + 1; bool button(int hit) { return hit == HTMINBUTTON || hit == HTMAXBUTTON || hit == HTCLOSE; } }
int Window::scale(int dip) const { return MulDiv(dip, GetDpiForWindow(hwnd_), 96); }
int Window::caption_height() const {
    RECT frame{}; const auto dpi = GetDpiForWindow(hwnd_);
    AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    const int border = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    return std::max<int>(scale(32), -frame.top - (IsZoomed(hwnd_) ? border : 0));
}
bool Window::create(HINSTANCE instance, bool direct2d, bool dark) {
    direct2d_ = direct2d; dark_ = dark;
    WNDCLASSEXW wc{sizeof(wc)}; wc.hInstance = instance; wc.lpfnWndProc = procedure;
    wc.lpszClassName = L"DRMusic.NativeShell"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101)); wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);
    WNDCLASSEXW caption_class{sizeof(caption_class)};
    caption_class.hInstance = instance; caption_class.lpfnWndProc = caption_procedure;
    caption_class.lpszClassName = L"DRMusic.CaptionRenderer";
    caption_class.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassExW(&caption_class);
    POINT pointer{}; GetCursorPos(&pointer); MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTOPRIMARY), &monitor)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &monitor.rcWork, 0);
    const auto r = monitor.rcWork;
    const int width = std::min<int>(1320, r.right - r.left), height = std::min<int>(890, r.bottom - r.top);
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, title_.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        r.left + (r.right - r.left - width) / 2, r.top + (r.bottom - r.top - height) / 2,
        width, height, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    // Keep the D2D swap chain bounded to the caption rather than allocating a
    // window-sized surface behind WebView2. Recreate it on caption resize.
    caption_ = CreateWindowExW(WS_EX_NOACTIVATE, caption_class.lpszClassName, L"", WS_CHILD,
        0, 0, 0, 0, hwnd_, nullptr, instance, this);
    if (!caption_) { DestroyWindow(hwnd_); return false; }
    apply_frame(); set_appearance(dark, false, title_);
    layout_caption();
    ShowWindow(hwnd_, SW_SHOW); UpdateWindow(hwnd_); return true;
}
RECT Window::content_bounds() const { RECT r{}; GetClientRect(hwnd_, &r); if (direct2d_) r.top = std::min(r.bottom, static_cast<LONG>(caption_height())); return r; }
void Window::layout_caption() {
    if (!caption_) return;
    RECT client{}; GetClientRect(hwnd_, &client);
    SetWindowPos(caption_, nullptr, 0, 0, client.right, caption_height(), SWP_NOACTIVATE | SWP_NOZORDER);
    ShowWindow(caption_, direct2d_ ? SW_SHOWNA : SW_HIDE); redraw();
}
void Window::redraw() { if (caption_) InvalidateRect(caption_, nullptr, FALSE); InvalidateRect(hwnd_, nullptr, FALSE); }
void Window::apply_frame() {
    const DWORD corners = 2; DwmSetWindowAttribute(hwnd_, 33, &corners, sizeof(corners));
    MARGINS margins{}; DwmExtendFrameIntoClientArea(hwnd_, &margins);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}
void Window::set_style(bool direct2d) {
    if (direct2d_ == direct2d) return;
    direct2d_ = direct2d; hot_ = pressed_ = HTNOWHERE; target_.Reset(); apply_frame();
    layout_caption(); if (on_resize) on_resize(); redraw();
}
void Window::set_appearance(bool dark, bool dimmed, const std::wstring &title) {
    dark_ = dark; dimmed_ = dimmed; title_ = title;
    BOOL value = dark_; DwmSetWindowAttribute(hwnd_, 20, &value, sizeof(value));
    SetWindowTextW(hwnd_, title_.c_str()); redraw();
}
void Window::post(std::function<void()> action) {
    { std::lock_guard lock(mutex_); pending_.push_back(std::move(action)); }
    PostMessageW(hwnd_, dispatch, 0, 0);
}
int Window::caption_hit(POINT p) const {
    RECT r{}; GetClientRect(hwnd_, &r);
    if (p.y < 0 || p.y >= caption_height() || p.x < 0 || p.x >= r.right) return HTCLIENT;
    auto width = scale(46);
    if (p.x >= r.right - width) return HTCLOSE;
    if (p.x >= r.right - width * 2) return HTMAXBUTTON;
    if (p.x >= r.right - width * 3) return HTMINBUTTON;
    return HTCAPTION;
}
void Window::hover(int hit, bool nonclient) {
    if (hot_ != hit) { hot_ = hit; redraw(); }
    TRACKMOUSEEVENT track{sizeof(track), static_cast<DWORD>(TME_LEAVE | (nonclient ? TME_NONCLIENT : 0)), hwnd_, 0}; TrackMouseEvent(&track);
}
void Window::paint(HWND surface) {
    PAINTSTRUCT ps{}; BeginPaint(surface, &ps); RECT rc{}; GetClientRect(surface, &rc);
    if (rc.right > 0 && rc.bottom > 0) {
        if (!factory_) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf());
        if (!write_) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(write_.GetAddressOf()));
        if (write_ && !text_) {
            // As in ../dr-reader/ui/src/window.cpp, select an installed family;
            // keep the same Noto-first order as the web UI's --font-ui stack.
            const wchar_t *family = L"Segoe UI";
            Microsoft::WRL::ComPtr<IDWriteFontCollection> fonts;
            if (SUCCEEDED(write_->GetSystemFontCollection(&fonts))) {
                for (const auto candidate : {L"Noto Sans SC", L"Noto Sans CJK SC", L"Microsoft YaHei", L"Microsoft YaHei UI", L"Segoe UI"}) {
                    UINT32 index = 0; BOOL exists = FALSE;
                    if (SUCCEEDED(fonts->FindFamilyName(candidate, &index, &exists)) && exists) { family = candidate; break; }
                }
            }
            write_->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12, L"zh-CN", &text_);
            if (text_) { text_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); text_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); }
        }
        if (factory_ && !target_) factory_->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(surface, D2D1::SizeU(rc.right, rc.bottom)), &target_);
        if (target_) {
            const auto dpi = static_cast<float>(GetDpiForWindow(hwnd_)); target_->SetDpi(dpi, dpi);
            const auto size = target_->GetSize(); const float height = caption_height() * 96.f / dpi;
            target_->BeginDraw(); target_->Clear(D2D1::ColorF(dark_ ? 0x141517 : 0xfaf9f7));
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
            target_->CreateSolidColorBrush(D2D1::ColorF(dark_ ? 0xededef : 0x262528), &brush);
            if (brush && direct2d_) {
                const auto fg = dark_ ? 0xededef : 0x262528;
                brush->SetColor(D2D1::ColorF(GetForegroundWindow() == hwnd_ ? fg : (dark_ ? 0x797b84 : 0x89858a)));
                if (text_) target_->DrawTextW(title_.c_str(), static_cast<UINT32>(title_.size()), text_.Get(), D2D1::RectF(14, 0, std::max(14.f, size.width - 150), height), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                for (int i = 0; i < 3; ++i) {
                    const int hit = i == 0 ? HTMINBUTTON : i == 1 ? HTMAXBUTTON : HTCLOSE;
                    const float x = size.width - 46.f * (3 - i), cx = x + 23, cy = height / 2;
                    if (hot_ == hit) { brush->SetColor(D2D1::ColorF(hit == HTCLOSE ? 0xc42b1c : dark_ ? 0x2b2c30 : 0xe7e5e2)); target_->FillRectangle(D2D1::RectF(x, 0, x + 46, height), brush.Get()); }
                    brush->SetColor(D2D1::ColorF(hot_ == HTCLOSE && hit == HTCLOSE ? 0xffffff : fg));
                    if (i == 0) target_->DrawLine(D2D1::Point2F(cx - 5, cy), D2D1::Point2F(cx + 5, cy), brush.Get());
                    else if (i == 2) { target_->DrawLine(D2D1::Point2F(cx - 5, cy - 5), D2D1::Point2F(cx + 5, cy + 5), brush.Get()); target_->DrawLine(D2D1::Point2F(cx - 5, cy + 5), D2D1::Point2F(cx + 5, cy - 5), brush.Get()); }
                    else {
                        if (IsZoomed(hwnd_)) { target_->DrawLine(D2D1::Point2F(cx - 2, cy - 5), D2D1::Point2F(cx + 5, cy - 5), brush.Get()); target_->DrawLine(D2D1::Point2F(cx + 5, cy - 5), D2D1::Point2F(cx + 5, cy + 2), brush.Get()); }
                        target_->DrawRectangle(D2D1::RectF(cx - 5, cy - 3, cx + 3, cy + 5), brush.Get());
                    }
                }
                if (dimmed_) { brush->SetColor(D2D1::ColorF(0x000000, .5f)); target_->FillRectangle(D2D1::RectF(0, 0, size.width, height), brush.Get()); }
            }
            if (target_->EndDraw() == D2DERR_RECREATE_TARGET) target_.Reset();
        }
    }
    EndPaint(surface, &ps);
}
LRESULT CALLBACK Window::caption_procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<Window *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) { self = static_cast<Window *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
    if (self) {
        if (m == WM_NCHITTEST) return HTTRANSPARENT;
        if (m == WM_ERASEBKGND) return 1;
        if (m == WM_PAINT) { self->paint(h); return 0; }
        if (m == WM_SIZE) { self->target_.Reset(); InvalidateRect(h, nullptr, FALSE); return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}
LRESULT CALLBACK Window::procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<Window *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) { self = static_cast<Window *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams); self->hwnd_ = h; SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
    return self ? self->message(m, w, l) : DefWindowProcW(h, m, w, l);
}
LRESULT Window::message(UINT m, WPARAM w, LPARAM l) {
    if (m == dispatch) {
        std::vector<std::function<void()>> work; { std::lock_guard lock(mutex_); work.swap(pending_); }
        for (auto &action : work) action(); return 0;
    }
    switch (m) {
    case WM_NCCALCSIZE:
        if (w && direct2d_) {
            auto p = reinterpret_cast<NCCALCSIZE_PARAMS *>(l); const auto top = p->rgrc[0].top;
            auto result = DefWindowProcW(hwnd_, m, w, l); const auto dpi = GetDpiForWindow(hwnd_);
            p->rgrc[0].top = top + (IsZoomed(hwnd_) ? GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi) : 0); return result;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; auto dc = BeginPaint(hwnd_, &ps); RECT rect{}; GetClientRect(hwnd_, &rect);
        auto brush = CreateSolidBrush(dark_ ? RGB(20,21,23) : RGB(250,249,247)); FillRect(dc, &rect, brush); DeleteObject(brush); EndPaint(hwnd_, &ps); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
        layout_caption(); if (on_resize) on_resize(); redraw(); return 0;
    case WM_DPICHANGED: {
        auto r = reinterpret_cast<RECT *>(l); target_.Reset();
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER); layout_caption(); if (on_resize) on_resize(); return 0;
    }
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO *>(l); MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor);
        info->ptMinTrackSize = {std::min<LONG>(scale(960), monitor.rcWork.right - monitor.rcWork.left), std::min<LONG>(scale(660), monitor.rcWork.bottom - monitor.rcWork.top)}; return 0;
    }
    case WM_ACTIVATE: redraw(); break;
    case WM_CLOSE: if (on_close) { on_close(); return 0; } break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    if (direct2d_) switch (m) {
    case WM_NCHITTEST: {
        const auto native = DefWindowProcW(hwnd_, m, w, l); if (native != HTCLIENT) return native;
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(hwnd_, &point);
        if (!IsZoomed(hwnd_) && point.y < scale(6)) return HTTOP;
        auto hit = caption_hit(point); return hit == HTMINBUTTON || hit == HTCLOSE ? HTCLIENT : hit;
    }
    case WM_NCMOUSEMOVE:
        hover(static_cast<int>(w), true); if (w != HTMAXBUTTON) return 0; break;
    case WM_NCMOUSELEAVE:
    case WM_MOUSELEAVE:
        if (pressed_ == HTNOWHERE) {
            POINT p{}; GetCursorPos(&p); ScreenToClient(hwnd_, &p);
            const auto hit = caption_hit(p);
            if (hot_ != hit) { hot_ = hit; redraw(); }
            // Do not re-arm TrackMouseEvent from a leave notification. When
            // the pointer is over the WebView child, re-arming immediately
            // queues another leave, starving WM_PAINT after maximization.
        }
        return 0;
    case WM_NCLBUTTONDOWN:
        if (w == HTMAXBUTTON) { hot_ = pressed_ = HTMAXBUTTON; SetCapture(hwnd_); return 0; } break;
    case WM_LBUTTONDOWN: {
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; auto hit = caption_hit(p);
        if (button(hit)) { hot_ = pressed_ = hit; SetCapture(hwnd_); redraw(); return 0; } break;
    }
    case WM_MOUSEMOVE: { POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; hover(caption_hit(p), false); break; }
    case WM_LBUTTONUP:
        if (pressed_ != HTNOWHERE) {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; auto hit = caption_hit(p), old = pressed_; pressed_ = HTNOWHERE; ReleaseCapture();
            if (hit == old) PostMessageW(hwnd_, WM_SYSCOMMAND, hit == HTCLOSE ? SC_CLOSE : hit == HTMINBUTTON ? SC_MINIMIZE : IsZoomed(hwnd_) ? SC_RESTORE : SC_MAXIMIZE, 0);
            redraw(); return 0;
        }
        break;
    case WM_CAPTURECHANGED: pressed_ = HTNOWHERE; redraw(); break;
    }
    return DefWindowProcW(hwnd_, m, w, l);
}
}
