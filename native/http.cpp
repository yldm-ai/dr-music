#include "http.h"
#include <winhttp.h>
#include <algorithm>
#include <cctype>
#include <fstream>
namespace dr {
namespace {
struct Handle {
    HINTERNET value = nullptr;
    explicit Handle(HINTERNET h) : value(h) {}
    ~Handle() { if (value) WinHttpCloseHandle(value); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    explicit operator bool() const { return value != nullptr; }
};
}

std::optional<Url> parse_url(const std::string &text) {
    Url url;
    std::string rest;
    if (text.starts_with("https://")) { url.https = true; rest = text.substr(8); }
    else if (text.starts_with("http://")) { url.https = false; rest = text.substr(7); }
    else return std::nullopt;
    const auto slash = rest.find('/');
    auto authority = rest.substr(0, slash);
    url.path = slash == std::string::npos ? "/" : rest.substr(slash);
    if (authority.empty() || authority.find('@') != std::string::npos) return std::nullopt;
    if (const auto colon = authority.find(':'); colon != std::string::npos) {
        const auto port = authority.substr(colon + 1);
        if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c); })) return std::nullopt;
        const auto value = std::stoul(port);
        if (!value || value > 65535) return std::nullopt;
        url.port = static_cast<unsigned short>(value); authority.resize(colon);
    }
    if (authority.empty() || !std::all_of(authority.begin(), authority.end(), [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '-'; })) return std::nullopt;
    std::transform(authority.begin(), authority.end(), authority.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    url.host = authority;
    if (!url.port) url.port = url.https ? 443 : 80;
    return url;
}

std::string url_encode(const std::string &text) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

void HttpCancel::cancel() {
    std::lock_guard lock(mutex_);
    cancelled_ = true;
    // Closing a request handle makes the blocking WinHTTP call on it return at once (documented for synchronous requests too).
    for (auto *handle : open_) WinHttpCloseHandle(static_cast<HINTERNET>(handle));
    open_.clear();
}
bool HttpCancel::cancelled() const { std::lock_guard lock(mutex_); return cancelled_; }
bool HttpCancel::track(void *handle) { std::lock_guard lock(mutex_); if (cancelled_) return false; open_.insert(handle); return true; }
bool HttpCancel::untrack(void *handle) { std::lock_guard lock(mutex_); return open_.erase(handle) > 0; }

HttpFetch winhttp_fetch(std::function<bool(const std::string &host)> allowed, std::shared_ptr<HttpCancel> cancel) {
    // The request handle is registered with `cancel`, which may close it from another thread; whoever unregisters it closes it.
    struct Request {
        HINTERNET value; HttpCancel *owner;
        Request(HINTERNET h, HttpCancel *c) : value(h), owner(c) {
            if (value && owner && !owner->track(value)) { WinHttpCloseHandle(value); value = nullptr; }
        }
        ~Request() { if (value && (!owner || owner->untrack(value))) WinHttpCloseHandle(value); }
        Request(const Request &) = delete;
        Request &operator=(const Request &) = delete;
        explicit operator bool() const { return value != nullptr; }
        void check() const { if (owner && owner->cancelled()) throw std::runtime_error("请求已取消"); }
    };
    return [allowed = std::move(allowed), cancel = std::move(cancel)](const HttpRequest &request) {
        if (cancel && cancel->cancelled()) throw std::runtime_error("请求已取消");
        const auto url = parse_url(request.url);
        if (!url) throw std::runtime_error("无效的歌词地址");
        if (!allowed || !allowed(url->host)) throw std::runtime_error("不允许访问的地址：" + url->host);
        Handle session(WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) DR-Music/0.2", request.direct ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session) throw std::runtime_error("无法初始化网络");
        WinHttpSetTimeouts(session.value, 5000, 5000, 8000, static_cast<int>(request.timeout_ms));
        Handle connection(WinHttpConnect(session.value, wide(url->host).c_str(), url->port, 0));
        if (!connection) throw std::runtime_error("无法连接 " + url->host);
        Request handle(WinHttpOpenRequest(connection.value, wide(request.method).c_str(), wide(url->path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url->https ? WINHTTP_FLAG_SECURE : 0), cancel.get());
        handle.check();
        if (!handle) throw std::runtime_error("无法连接 " + url->host);
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(handle.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(handle.value, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
        std::wstring headers;
        for (const auto &[name, value] : request.headers) headers += wide(name) + L": " + wide(value) + L"\r\n";
        const BOOL sent = WinHttpSendRequest(handle.value, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), headers.empty() ? 0 : static_cast<DWORD>(-1L),
            request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char *>(request.body.data()), static_cast<DWORD>(request.body.size()), static_cast<DWORD>(request.body.size()), 0);
        if (!sent || !WinHttpReceiveResponse(handle.value, nullptr)) throw std::runtime_error("无法连接 " + url->host);
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(handle.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        HttpResponse response{static_cast<int>(status), {}};
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(handle.value, &available)) throw std::runtime_error("下载失败：" + url->host);
            if (!available) break;
            if (request.on_data) {
                std::string chunk(available, '\0'); DWORD read = 0;
                if (!WinHttpReadData(handle.value, chunk.data(), available, &read)) throw std::runtime_error("下载失败：" + url->host);
                request.on_data(std::string_view(chunk.data(), read));
                continue;
            }
            if (response.body.size() + available > request.limit) throw std::runtime_error("响应过大：" + url->host);
            const auto offset = response.body.size(); response.body.resize(offset + available);
            DWORD read = 0;
            if (!WinHttpReadData(handle.value, response.body.data() + offset, available, &read)) throw std::runtime_error("下载失败：" + url->host);
            response.body.resize(offset + read);
        }
        return response;
    };
}

HttpFetch fixture_fetch(fs::path root) {
    return [root = std::move(root)](const HttpRequest &request) {
        const auto url = parse_url(request.url);
        if (!url) throw std::runtime_error("无效的歌词地址");
        auto path = url->path.substr(0, url->path.find('?'));
        if (path.find("..") != std::string::npos) return HttpResponse{404, {}};
        const auto file = root / from_text(url->host) / from_text(path.substr(1));
        std::error_code ec;
        if (!fs::is_regular_file(file, ec)) return HttpResponse{404, {}};
        auto body = read_text(file, request.limit);
        if (request.on_data) { request.on_data(body); return HttpResponse{200, {}}; }
        return HttpResponse{200, std::move(body)};
    };
}
}
