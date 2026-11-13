#include "window.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
namespace dr {
namespace { constexpr UINT dispatch = WM_APP + 1; constexpr UINT_PTR caption_tip_timer = 43; bool button(int hit) { return hit == HTMINBUTTON || hit == HTMAXBUTTON || hit == HTCLOSE; } }
int Window::scale(int dip) const { return MulDiv(dip, GetDpiForWindow(hwnd_), 96); }
int Window::caption_height() const {
    RECT frame{}; const auto dpi = GetDpiForWindow(hwnd_);
    AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    const int border = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    return std::max<int>(scale(32), -frame.top - (IsZoomed(hwnd_) ? border : 0));
}
bool Window::create(HINSTANCE instance, bool direct2d, bool dark, bool cloaked) {
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
    const int width = std::min<int>(1520, r.right - r.left), height = std::min<int>(1248, r.bottom - r.top);
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, title_.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        r.left + (r.right - r.left - width) / 2, r.top + (r.bottom - r.top - height) / 2,
        width, height, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    // Keep the D2D swap chain bounded to the caption rather than allocating a
    // window-sized surface behind WebView2. Recreate it on caption resize.
    caption_ = CreateWindowExW(WS_EX_NOACTIVATE, caption_class.lpszClassName, L"", WS_CHILD,
        0, 0, 0, 0, hwnd_, nullptr, instance, this);
    if (!caption_) { DestroyWindow(hwnd_); return false; }
    if (cloaked) {
        const BOOL cloak = TRUE;
        if (FAILED(DwmSetWindowAttribute(hwnd_, DWMWA_CLOAK, &cloak, sizeof(cloak)))) {
            DestroyWindow(hwnd_); return false;
        }
    }
    apply_frame(); set_appearance(dark, false);
    layout_caption();
    ShowWindow(hwnd_, SW_SHOW); UpdateWindow(hwnd_); return true;
}
RECT Window::content_bounds() const { RECT r{}; GetClientRect(hwnd_, &r); if (direct2d_) r.top = std::min(r.bottom, static_cast<LONG>(caption_height())); return r; }
void Window::layout_caption() {
    caption_tip(HTNOWHERE);
    if (!caption_) return;
    RECT client{}; GetClientRect(hwnd_, &client);
    SetWindowPos(caption_, nullptr, 0, 0, client.right, caption_height(), SWP_NOACTIVATE | SWP_NOZORDER);
    ShowWindow(caption_, direct2d_ ? SW_SHOWNA : SW_HIDE); redraw();
}
void Window::clear_web_hover() {
    if (web_mouse_inside_) {
        web_mouse_inside_ = false;
        if (on_mouse) on_mouse(WM_MOUSELEAVE, 0, 0);
    }
    if (caption_hot_ || caption_pressed_) {
        caption_hot_ = caption_pressed_ = false;
        caption_tip(HTNOWHERE);
        if (on_caption_event) on_caption_event(L"leave");
    }
}
void Window::set_caption_layout(RECT caption, RECT minimize, RECT maximize, RECT close) {
    RECT client{}; GetClientRect(hwnd_, &client);
    // WebView2 rounds its CSS viewport to the nearest device pixel, so the
    // page's right/bottom edge can be one pixel past the host's client rect.
    constexpr LONG tolerance = 2;
    auto valid = [&](RECT r) {
        return r.left >= -tolerance && r.top >= -tolerance && r.right > r.left && r.bottom > r.top &&
            r.right <= client.right + tolerance && r.bottom <= client.bottom + tolerance;
    };
    if (!valid(caption) || !valid(minimize) || !valid(maximize) || !valid(close)) return;
    auto clamp = [&](RECT r) {
        r.left = std::clamp<LONG>(r.left, 0, client.right);
        r.top = std::clamp<LONG>(r.top, 0, client.bottom);
        r.right = std::clamp<LONG>(r.right, r.left + 1, client.right);
        r.bottom = std::clamp<LONG>(r.bottom, r.top + 1, client.bottom);
        return r;
    };
    caption_rect_ = clamp(caption); minimize_rect_ = clamp(minimize); maximize_rect_ = clamp(maximize); close_rect_ = clamp(close);
    caption_layout_ready_ = true;
}
void Window::redraw() { if (caption_) InvalidateRect(caption_, nullptr, FALSE); InvalidateRect(hwnd_, nullptr, FALSE); }
void Window::apply_frame() {
    const DWORD corners = 2; DwmSetWindowAttribute(hwnd_, 33, &corners, sizeof(corners));
    MARGINS margins{}; DwmExtendFrameIntoClientArea(hwnd_, &margins);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}
void Window::set_style(bool direct2d, bool webview) {
    if (direct2d_ == direct2d && web_caption_ == webview) return;
    caption_tip(HTNOWHERE);
    clear_web_hover();
    caption_layout_ready_ = false;
    direct2d_ = direct2d; web_caption_ = webview; hot_ = pressed_ = HTNOWHERE; target_.Reset(); apply_frame();
    layout_caption(); if (on_resize) on_resize(); redraw();
}
void Window::set_appearance(bool dark, bool dimmed) {
    caption_tip(HTNOWHERE);
    dark_ = dark; dimmed_ = dimmed;
    BOOL value = dark_; DwmSetWindowAttribute(hwnd_, 20, &value, sizeof(value));
    SetWindowTextW(hwnd_, title_.c_str()); redraw();
}
void Window::post(std::function<void()> action) {
    { std::lock_guard lock(mutex_); pending_.push_back(std::move(action)); }
    PostMessageW(hwnd_, dispatch, 0, 0);
}
int Window::caption_hit(POINT p) const {
    RECT r{}; GetClientRect(hwnd_, &r);
    if (p.x < 0 || p.y < 0 || p.x >= r.right || p.y >= r.bottom) return HTCLIENT;
    if (web_caption_) {
        auto inside = [&](const RECT &rect) { return p.x >= rect.left && p.x < rect.right && p.y >= rect.top && p.y < rect.bottom; };
        if (caption_layout_ready_) {
            if (inside(maximize_rect_)) return HTMAXBUTTON;
            if (inside(minimize_rect_) || inside(close_rect_)) return HTCLIENT;
            if (p.y >= caption_rect_.top && p.y < caption_rect_.bottom) return HTCAPTION;
            return HTCLIENT;
        }
        const int width = scale(46), height = scale(36);
        if (p.y >= height) return HTCLIENT;
        if (p.x >= r.right - width) return HTCLIENT;
        if (p.x >= r.right - width * 2) return HTMAXBUTTON;
        if (p.x >= r.right - width * 3) return HTCLIENT;
        return HTCAPTION;
    }
    if (p.y >= caption_height()) return HTCLIENT;
    auto width = scale(46);
    if (p.x >= r.right - width) return HTCLOSE;
    if (p.x >= r.right - width * 2) return HTMAXBUTTON;
    if (p.x >= r.right - width * 3) return HTMINBUTTON;
    return HTCAPTION;
}
void Window::hover(int hit, bool nonclient) {
    if (hot_ != hit) { hot_ = hit; redraw(); }
    if (pressed_ == HTNOWHERE && !dimmed_) caption_tip(hit);
    TRACKMOUSEEVENT track{sizeof(track), static_cast<DWORD>(TME_LEAVE | (nonclient ? TME_NONCLIENT : 0)), hwnd_, 0}; TrackMouseEvent(&track);
}
RECT Window::caption_button(int hit) const {
    RECT r{}; GetClientRect(hwnd_, &r);
    if (web_caption_ && caption_layout_ready_) {
        if (hit == HTMAXBUTTON) return maximize_rect_;
        if (hit == HTMINBUTTON) return minimize_rect_;
        if (hit == HTCLOSE) return close_rect_;
    }
    const int width = scale(46), slot = hit == HTCLOSE ? 1 : hit == HTMAXBUTTON ? 2 : 3;
    const int height = web_caption_ ? scale(36) : caption_height();
    return {r.right - width * slot, 0, r.right - width * (slot - 1), height};
}
void Window::caption_tip(int hit) {
    // Same delayed native-caption -> web tooltip handoff as DR Dict / DR Reader.
    const auto next = button(hit) && (direct2d_ || web_caption_) && !dimmed_ ? hit : HTNOWHERE;
    if (tip_ == next) return;
    tip_ = next; KillTimer(hwnd_, caption_tip_timer);
    if (on_caption_tooltip) on_caption_tooltip(L"", RECT{});
    if (tip_ != HTNOWHERE) SetTimer(hwnd_, caption_tip_timer, 500, nullptr);
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
            write_->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13, L"zh-CN", &text_);
            if (text_) { text_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); text_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); }
        }
        if (write_ && !icons_) {
            // Windows' own caption glyphs (Segoe Fluent Icons on Windows 11, Segoe MDL2 Assets on 10), 10 DIP like the system caption.
            const wchar_t *family = L"Segoe MDL2 Assets";
            Microsoft::WRL::ComPtr<IDWriteFontCollection> fonts; UINT32 index = 0; BOOL exists = FALSE;
            if (SUCCEEDED(write_->GetSystemFontCollection(&fonts)) && SUCCEEDED(fonts->FindFamilyName(L"Segoe Fluent Icons", &index, &exists)) && exists) family = L"Segoe Fluent Icons";
            write_->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10, L"", &icons_);
            if (icons_) { icons_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); icons_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); icons_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); }
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
                    const float x = size.width - 46.f * (3 - i);
                    if (hot_ == hit) { brush->SetColor(D2D1::ColorF(hit == HTCLOSE ? 0xc42b1c : dark_ ? 0x2b2c30 : 0xe7e5e2)); target_->FillRectangle(D2D1::RectF(x, 0, x + 46, height), brush.Get()); }
                    brush->SetColor(D2D1::ColorF(hot_ == HTCLOSE && hit == HTCLOSE ? 0xffffff : hot_ == hit || GetForegroundWindow() == hwnd_ ? fg : (dark_ ? 0x797b84 : 0x89858a)));
                    // ChromeMinimize / ChromeMaximize / ChromeRestore / ChromeClose.
                    const wchar_t glyph = i == 0 ? L'\xE921' : i == 2 ? L'\xE8BB' : IsZoomed(hwnd_) ? L'\xE923' : L'\xE922';
                    if (icons_) target_->DrawTextW(&glyph, 1, icons_.Get(), D2D1::RectF(x, 0, x + 46, height), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
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
        if (w && (direct2d_ || web_caption_)) {
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
        if (w == SIZE_MINIMIZED) clear_web_hover();
        caption_layout_ready_ = false;
        layout_caption(); if (on_resize) on_resize(); redraw(); return 0;
    case WM_DPICHANGED: {
        auto r = reinterpret_cast<RECT *>(l); target_.Reset(); caption_layout_ready_ = false;
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER); layout_caption(); if (on_resize) on_resize(); return 0;
    }
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO *>(l); MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor);
        info->ptMinTrackSize = {std::min<LONG>(scale(960), monitor.rcWork.right - monitor.rcWork.left), std::min<LONG>(scale(660), monitor.rcWork.bottom - monitor.rcWork.top)}; return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) { clear_web_hover(); caption_tip(HTNOWHERE); }
        else if (on_focus) on_focus();
        redraw(); break;
    case WM_SETFOCUS:
        if (on_focus) on_focus();
        break;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && on_cursor && on_cursor()) return TRUE;
        break;
    case WM_MOVE:
        caption_tip(HTNOWHERE);
        if (on_move) on_move();
        break;
    case WM_TIMER:
        if (w == caption_tip_timer) {
            KillTimer(hwnd_, caption_tip_timer);
            if ((direct2d_ || web_caption_) && !dimmed_ && tip_ != HTNOWHERE && pressed_ == HTNOWHERE && on_caption_tooltip)
                on_caption_tooltip(tip_ == HTMINBUTTON ? L"最小化" : tip_ == HTCLOSE ? L"关闭" : IsZoomed(hwnd_) ? L"向下还原" : L"最大化", caption_button(tip_));
            return 0;
        }
        break;
    case WM_CLOSE: if (on_close) { on_close(); return 0; } break;
    case WM_DESTROY: KillTimer(hwnd_, caption_tip_timer); PostQuitMessage(0); return 0;
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
        if (pressed_ == HTNOWHERE) {
            POINT p{}; GetCursorPos(&p); ScreenToClient(hwnd_, &p);
            const auto hit = caption_hit(p);
            if (hot_ != hit) { hot_ = hit; redraw(); }
            caption_tip(hit);
            // Do not re-arm TrackMouseEvent from a leave notification. When
            // the pointer is over the WebView child, re-arming immediately
            // queues another leave, starving WM_PAINT after maximization.
        }
        return 0;
    case WM_MOUSELEAVE:
        if (pressed_ == HTNOWHERE) {
            POINT p{}; GetCursorPos(&p); ScreenToClient(hwnd_, &p);
            const auto hit = caption_hit(p);
            if (hot_ != hit) { hot_ = hit; redraw(); }
            caption_tip(hit);
        }
        if (on_mouse) on_mouse(m, w, l);
        return 0;
    case WM_NCLBUTTONDOWN:
        if (w == HTMAXBUTTON) { caption_tip(HTNOWHERE); hot_ = pressed_ = HTMAXBUTTON; SetCapture(hwnd_); return 0; } break;
    case WM_LBUTTONDOWN: {
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; auto hit = caption_hit(p);
        if (button(hit)) { caption_tip(HTNOWHERE); hot_ = pressed_ = hit; SetCapture(hwnd_); redraw(); return 0; } break;
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
    if (web_caption_) switch (m) {
    case WM_NCHITTEST: {
        const auto native = DefWindowProcW(hwnd_, m, w, l); if (native != HTCLIENT) return native;
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(hwnd_, &point);
        if (!IsZoomed(hwnd_) && point.y < scale(6)) return HTTOP;
        return caption_hit(point);
    }
    case WM_NCMOUSEMOVE: {
        if (web_mouse_inside_) {
            web_mouse_inside_ = false;
            if (on_mouse) on_mouse(WM_MOUSELEAVE, 0, 0);
        }
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(hwnd_, &point);
        const bool hot = caption_hit(point) == HTMAXBUTTON;
        if (caption_hot_ != hot) {
            caption_hot_ = hot;
            caption_tip(hot ? HTMAXBUTTON : HTNOWHERE);
            if (on_caption_event) on_caption_event(hot ? L"enter" : L"leave");
        }
        if (hot) {
            TRACKMOUSEEVENT track{sizeof(track), TME_NONCLIENT | TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&track);
            return 0;
        }
        break;
    }
    case WM_NCMOUSELEAVE:
        if (caption_pressed_) break;
        if (caption_hot_) {
            caption_hot_ = false;
            caption_tip(HTNOWHERE);
            if (on_caption_event) on_caption_event(L"leave");
        }
        return 0;
    case WM_NCLBUTTONDOWN: {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(hwnd_, &point);
        if (caption_hit(point) == HTMAXBUTTON) {
            caption_hot_ = caption_pressed_ = true;
            caption_tip(HTNOWHERE);
            if (on_caption_event) on_caption_event(L"down");
            return 0;
        }
        break;
    }
    case WM_NCLBUTTONUP: {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(hwnd_, &point);
        if (caption_pressed_) {
            caption_pressed_ = false;
            if (on_caption_event) on_caption_event(L"up");
            if (caption_hit(point) == HTMAXBUTTON)
                PostMessageW(hwnd_, WM_SYSCOMMAND, IsZoomed(hwnd_) ? SC_RESTORE : SC_MAXIMIZE, 0);
            return 0;
        }
        break;
    }
    case WM_CAPTURECHANGED:
        if (caption_pressed_) {
            caption_pressed_ = false;
            if (on_caption_event) on_caption_event(L"up");
        }
        break;
    case WM_LBUTTONDOWN:
        if (on_focus) on_focus();
        [[fallthrough]];
    case WM_MOUSEMOVE:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (on_mouse) { web_mouse_inside_ = true; on_mouse(m, w, l); return 0; }
        break;
    case WM_MOUSELEAVE:
        web_mouse_inside_ = false;
        if (on_mouse) on_mouse(m, w, l);
        if (!caption_hot_) caption_tip(HTNOWHERE);
        return 0;
    }
    if (on_mouse) switch (m) {
    case WM_MOUSELEAVE:
        web_mouse_inside_ = false;
        on_mouse(m, w, l); return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (m == WM_MOUSEWHEEL || m == WM_MOUSEHWHEEL) ScreenToClient(hwnd_, &point);
        const int top = direct2d_ ? caption_height() : web_caption_ ? (caption_layout_ready_ ? caption_rect_.bottom : scale(36)) : 0;
        if (point.y >= top) { web_mouse_inside_ = true; on_mouse(m, w, l); return 0; }
        break;
    }
    }
    return DefWindowProcW(hwnd_, m, w, l);
}
}
