#include "webview.h"
#include <WebView2EnvironmentOptions.h>
#include <sstream>
namespace dr {
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
void WebView::fail(HRESULT error, const wchar_t *step) {
    std::wostringstream text; text << step << L"失败 (0x" << std::hex << error << L")。请安装或修复 Microsoft Edge WebView2 Runtime。";
    if (on_error) on_error(text.str());
}
void WebView::resize(RECT bounds) { bounds_ = bounds; if (controller_) controller_->put_Bounds(bounds); }
void WebView::send(const Json &message) { if (view_) view_->PostWebMessageAsJson(wide(message.dump(-1, ' ', false, Json::error_handler_t::replace)).c_str()); }
void WebView::close() { if (controller_) controller_->Close(); view_.Reset(); controller_.Reset(); environment_.Reset(); }
void WebView::create(HWND window, const fs::path &web_root, const fs::path &data_root, unsigned debug_port) {
    debugging_ = debug_port != 0;
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    if (debugging_) {
        auto args = L"--remote-debugging-port=" + std::to_wstring(debug_port) + L" --remote-debugging-address=127.0.0.1";
        options->put_AdditionalBrowserArguments(args.c_str());
    }
    auto hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, (data_root / L"webview-profile").c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this, window, web_root](HRESULT result, ICoreWebView2Environment *env) -> HRESULT {
            if (FAILED(result) || !env) { fail(result, L"WebView2 初始化"); return S_OK; }
            environment_ = env;
            auto requested = env->CreateCoreWebView2Controller(window, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [this, web_root](HRESULT created, ICoreWebView2Controller *controller) -> HRESULT {
                    if (FAILED(created) || !controller) { fail(created, L"WebView2 控制器创建"); return S_OK; }
                    controller_ = controller; controller_->get_CoreWebView2(&view_);
                    ComPtr<ICoreWebView2Controller2> v2;
                    if (SUCCEEDED(controller_.As(&v2))) v2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{255, 20, 21, 23});
                    controller_->put_Bounds(bounds_); configure(web_root); return S_OK;
                }).Get());
            if (FAILED(requested)) fail(requested, L"WebView2 控制器请求"); return S_OK;
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
