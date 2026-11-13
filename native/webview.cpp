#include "webview.h"
#include <WebView2EnvironmentOptions.h>
#include <windowsx.h>
#include <sstream>
namespace dr {
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace {
UINT32 mouse_keys(WPARAM value) {
    UINT32 keys = COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE;
    if (value & MK_LBUTTON) keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON;
    if (value & MK_RBUTTON) keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_RIGHT_BUTTON;
    if (value & MK_MBUTTON) keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_MIDDLE_BUTTON;
    if (value & MK_SHIFT) keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_SHIFT;
    if (value & MK_CONTROL) keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_CONTROL;
    if (value & MK_XBUTTON1) keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_X_BUTTON1;
    if (value & MK_XBUTTON2) keys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_X_BUTTON2;
    return keys;
}
}
void WebView::fail(HRESULT error, const wchar_t *step) {
    std::wostringstream text; text << step << L"失败 (0x" << std::hex << error << L")。请安装或修复 Microsoft Edge WebView2 Runtime。";
    if (on_error) on_error(text.str());
}
void WebView::resize(RECT bounds) {
    bounds_ = bounds;
    if (!controller_) return;
    controller_->put_Bounds(bounds);
    // A composition-hosted WebView is not moved by put_Bounds: its visual is placed (and clipped)
    // explicitly, as in the WebView2 sample's ViewComponent::ResizeWebView. Otherwise the page stays
    // at the client origin, covers the Direct2D caption and leaves the bottom strip blank.
    if (composition_root_ && composition_device_) {
        composition_root_->SetOffsetX(static_cast<float>(bounds.left));
        composition_root_->SetOffsetY(static_cast<float>(bounds.top));
        composition_root_->SetClip(D2D_RECT_F{0, 0, static_cast<float>(bounds.right - bounds.left), static_cast<float>(bounds.bottom - bounds.top)});
        composition_device_->Commit();
    }
}
void WebView::send(const Json &message) { if (view_) view_->PostWebMessageAsJson(wide(message.dump(-1, ' ', false, Json::error_handler_t::replace)).c_str()); }
void WebView::close() {
    if (controller_) controller_->Close();
    composition_root_.Reset(); composition_target_.Reset(); composition_device_.Reset();
    composition_controller_.Reset(); view_.Reset(); controller_.Reset(); environment_.Reset();
}
void WebView::focus() {
    if (controller_) controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}
void WebView::notify_move() {
    if (controller_) controller_->NotifyParentWindowPositionChanged();
}
bool WebView::cursor() {
    if (!composition_controller_) return false;
    UINT32 id = 0;
    if (SUCCEEDED(composition_controller_->get_SystemCursorId(&id)) && id) {
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(id)));
        return true;
    }
    return false;
}
void WebView::mouse(UINT message, WPARAM wParam, LPARAM lParam) {
    if (!composition_controller_) return;
    COREWEBVIEW2_MOUSE_EVENT_KIND kind{};
    UINT32 data = 0;
    switch (message) {
    case WM_MOUSEMOVE: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE; break;
    case WM_LBUTTONDOWN: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN; break;
    case WM_LBUTTONUP: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP; break;
    case WM_LBUTTONDBLCLK: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOUBLE_CLICK; break;
    case WM_RBUTTONDOWN: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOWN; break;
    case WM_RBUTTONUP: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP; break;
    case WM_RBUTTONDBLCLK: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOUBLE_CLICK; break;
    case WM_MBUTTONDOWN: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOWN; break;
    case WM_MBUTTONUP: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP; break;
    case WM_MBUTTONDBLCLK: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOUBLE_CLICK; break;
    case WM_XBUTTONDOWN: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_DOWN; data = GET_XBUTTON_WPARAM(wParam); break;
    case WM_XBUTTONUP: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_UP; data = GET_XBUTTON_WPARAM(wParam); break;
    case WM_XBUTTONDBLCLK: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_DOUBLE_CLICK; data = GET_XBUTTON_WPARAM(wParam); break;
    case WM_MOUSEWHEEL: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL; data = static_cast<UINT32>(GET_WHEEL_DELTA_WPARAM(wParam)); break;
    case WM_MOUSEHWHEEL: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_HORIZONTAL_WHEEL; data = static_cast<UINT32>(GET_WHEEL_DELTA_WPARAM(wParam)); break;
    case WM_MOUSELEAVE: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE; break;
    default: return;
    }
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) ScreenToClient(window_, &point);
    // Mouse input is relative to the WebView, which starts below the Direct2D caption.
    point.x -= bounds_.left; point.y -= bounds_.top;
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window_, 0};
        TrackMouseEvent(&track);
    }
    composition_controller_->SendMouseInput(kind, static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(mouse_keys(wParam)), data, point);
}
void WebView::create(HWND window, const fs::path &web_root, const fs::path &data_root, unsigned debug_port) {
    window_ = window;
    debugging_ = debug_port != 0;
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    // As in ../dr-reader/app/src/webview.cpp, keep composited scrollers in the
    // main layer. Otherwise Chromium renders their Chinese text with greyscale
    // antialiasing instead of ClearType, making the settings dialog look soft.
    auto args = std::wstring(L"--disable-prefer-compositing-to-lcd-text");
    if (debugging_) args += L" --remote-debugging-port=" + std::to_wstring(debug_port) + L" --remote-debugging-address=127.0.0.1";
    options->put_AdditionalBrowserArguments(args.c_str());
    auto hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, (data_root / L"webview-profile").c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this, web_root](HRESULT result, ICoreWebView2Environment *env) -> HRESULT {
            if (FAILED(result) || !env) { fail(result, L"WebView2 初始化"); return S_OK; }
            environment_ = env;
            ComPtr<ICoreWebView2Environment3> env3;
            if (FAILED(env->QueryInterface(IID_PPV_ARGS(&env3)))) { fail(E_NOINTERFACE, L"WebView2 合成控制器接口"); return S_OK; }
            auto requested = env3->CreateCoreWebView2CompositionController(window_, Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                [this, web_root](HRESULT created, ICoreWebView2CompositionController *controller) -> HRESULT {
                    if (FAILED(created) || !controller) { fail(created, L"WebView2 合成控制器创建"); return S_OK; }
                    composition_controller_ = controller;
                    if (FAILED(composition_controller_.As(&controller_))) { fail(E_NOINTERFACE, L"WebView2 控制器接口"); return S_OK; }
                    if (FAILED(controller_->get_CoreWebView2(&view_)) || !view_) { fail(E_FAIL, L"WebView2 核心接口"); return S_OK; }
                    ComPtr<ICoreWebView2Controller2> v2;
                    if (SUCCEEDED(controller_.As(&v2))) v2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{255, 20, 21, 23});
                    if (FAILED(DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&composition_device_))) ||
                        FAILED(composition_device_->CreateTargetForHwnd(window_, TRUE, &composition_target_)) ||
                        FAILED(composition_device_->CreateVisual(&composition_root_)) ||
                        FAILED(composition_target_->SetRoot(composition_root_.Get())) ||
                        FAILED(composition_controller_->put_RootVisualTarget(composition_root_.Get())) ||
                        FAILED(composition_device_->Commit())) {
                        fail(E_FAIL, L"WebView2 合成视觉树");
                        return S_OK;
                    }
                    resize(bounds_);
                    configure(web_root);
                    if (on_ready) on_ready();
                    return S_OK;
                }).Get());
            if (FAILED(requested)) fail(requested, L"WebView2 合成控制器请求");
            return S_OK;
        }).Get());
    if (FAILED(hr)) fail(hr, L"WebView2 环境请求");
}
void WebView::configure(const fs::path &web_root) {
    // The same virtual-host mapping / PostWebMessage boundary used by DR Dict.
    ComPtr<ICoreWebView2_3> v3;
    if (FAILED(view_.As(&v3))) { fail(E_NOINTERFACE, L"WebView2 本地资源映射"); return; }
    auto hr = v3->SetVirtualHostNameToFolderMapping(L"app.dr-music.local", web_root.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    if (FAILED(hr)) { fail(hr, L"WebView2 本地目录映射"); return; }
    ComPtr<ICoreWebView2Settings> settings; view_->get_Settings(&settings);
    settings->put_IsStatusBarEnabled(FALSE); settings->put_AreHostObjectsAllowed(FALSE);
    settings->put_IsZoomControlEnabled(FALSE); settings->put_AreDevToolsEnabled(debugging_);
    settings->put_AreDefaultContextMenusEnabled(FALSE);
    ComPtr<ICoreWebView2Settings3> settings3;
    if (SUCCEEDED(settings.As(&settings3))) settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
    EventRegistrationToken token{};
    view_->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([this](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
        LPWSTR source = nullptr, raw = nullptr; args->get_Source(&source); args->get_WebMessageAsJson(&raw);
        auto origin = take(source); auto message = take(raw);
        if (origin != app_index || message.size() > 8 * 1024 * 1024) return S_OK;
        try { if (on_message) on_message(Json::parse(utf8(message))); } catch (const std::exception &) { /* untrusted messages do not escape COM */ }
        return S_OK;
    }).Get(), &token);
    view_->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
        LPWSTR uri = nullptr; args->get_Uri(&uri); if (take(uri) != app_index) args->put_Cancel(TRUE); return S_OK;
    }).Get(), &token);
    view_->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT { args->put_Cancel(TRUE); return S_OK; }).Get(), &token);
    view_->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT { args->put_Handled(TRUE); return S_OK; }).Get(), &token);
    view_->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2 *, ICoreWebView2PermissionRequestedEventArgs *args) -> HRESULT { args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK; }).Get(), &token);
    view_->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    view_->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>([this](ICoreWebView2 *, ICoreWebView2WebResourceRequestedEventArgs *args) -> HRESULT {
        ComPtr<ICoreWebView2WebResourceRequest> request; args->get_Request(&request);
        LPWSTR raw_uri = nullptr, raw_method = nullptr; request->get_Uri(&raw_uri); request->get_Method(&raw_method);
        const auto uri = take(raw_uri); const auto method = utf8(take(raw_method));
        // App assets are served from the folder mapping. All other network
        // destinations are answered here, never forwarded to the Internet.
        if (uri.starts_with(L"https://app.dr-music.local/")) return S_OK;
        MediaResponse reply;
        if (uri.starts_with(L"https://media.dr-music.local/") && on_resource) {
            ComPtr<ICoreWebView2HttpRequestHeaders> headers; request->get_Headers(&headers);
            LPWSTR range = nullptr; headers->GetHeader(L"Range", &range);
            reply = on_resource(uri, utf8(take(range)), method);
        } else reply.status = 403;
        const auto reason = reply.status == 200 ? L"OK" : reply.status == 206 ? L"Partial Content" : reply.status == 416 ? L"Range Not Satisfiable" : reply.status == 403 ? L"Forbidden" : L"Not Found";
        ComPtr<ICoreWebView2WebResourceResponse> response;
        if (SUCCEEDED(environment_->CreateWebResourceResponse(reply.stream.Get(), reply.status, reason, reply.headers.c_str(), &response))) args->put_Response(response.Get());
        return S_OK;
    }).Get(), &token);
    view_->Navigate(app_index);
}
}
