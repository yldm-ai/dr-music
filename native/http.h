#pragma once
#include "common.h"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace dr {
struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    size_t limit = 8 * 1024 * 1024;
    unsigned timeout_ms = 15000; // receive timeout; a local model may need longer to answer
    bool direct = false;         // bypass any system proxy (loopback services)
    /** Streaming: when set, body chunks are handed over as they arrive instead of being collected. */
    std::function<void(std::string_view)> on_data;
    /** Streamed upload: the request body is this file (sent in chunks), `on_sent` reports bytes sent. */
    fs::path body_file;
    std::function<void(uint64_t sent, uint64_t total)> on_sent;
};
struct HttpResponse { int status = 0; std::string body; };
/** Performs a request; throws on transport failure, returns any HTTP status. */
using HttpFetch = std::function<HttpResponse(const HttpRequest &)>;

struct Url { bool https = true; std::string host; unsigned short port = 0; std::string path; /* path + query */ };
std::optional<Url> parse_url(const std::string &url);
/** Percent-encodes everything except RFC 3986 unreserved characters. */
std::string url_encode(const std::string &text);

/**
 * Shared by fetchers: `cancel()` aborts their in-flight requests and makes later ones fail at once,
 * so workers joined on exit never wait for a slow server (a local model can take minutes to answer).
 */
class HttpCancel {
public:
    void cancel();
    bool cancelled() const;
    /** Registers an open request handle; false (and nothing registered) once cancelled. */
    bool track(void *handle);
    /** True when the caller still owns the handle and must close it; false when cancel() already closed it. */
    bool untrack(void *handle);
private:
    mutable std::mutex mutex_;
    bool cancelled_ = false;
    std::set<void *> open_; // request handles in flight; cancel() closes them
};
/**
 * WinHTTP client for online lyrics. Only hosts accepted by `allowed` are contacted, redirects are
 * never followed, bodies are size-capped and timeouts are short.
 */
HttpFetch winhttp_fetch(std::function<bool(const std::string &host)> allowed, std::shared_ptr<HttpCancel> cancel = {});
/**
 * Test double: serves `root/<host>/<path without query>` for every request (404 when missing), so
 * provider code runs end to end without the network.
 */
HttpFetch fixture_fetch(fs::path root);
}
