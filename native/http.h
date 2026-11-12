#pragma once
#include "common.h"
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace dr {
struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    size_t limit = 8 * 1024 * 1024;
};
struct HttpResponse { int status = 0; std::string body; };
/** Performs a request; throws on transport failure, returns any HTTP status. */
using HttpFetch = std::function<HttpResponse(const HttpRequest &)>;

struct Url { bool https = true; std::string host; unsigned short port = 0; std::string path; /* path + query */ };
std::optional<Url> parse_url(const std::string &url);
/** Percent-encodes everything except RFC 3986 unreserved characters. */
std::string url_encode(const std::string &text);

/**
 * WinHTTP client for online lyrics. Only hosts accepted by `allowed` are contacted, redirects are
 * never followed, bodies are size-capped and timeouts are short (the app joins this worker on exit).
 */
HttpFetch winhttp_fetch(std::function<bool(const std::string &host)> allowed);
/**
 * Test double: serves `root/<host>/<path without query>` for every request (404 when missing), so
 * provider code runs end to end without the network.
 */
HttpFetch fixture_fetch(fs::path root);
}
