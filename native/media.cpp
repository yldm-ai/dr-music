#include "media.h"
#include <algorithm>
#include <charconv>
#include <cstring>
namespace dr {
std::optional<ByteRange> byte_range(const std::string &header, uint64_t size) {
    if (!size) return {};
    if (header.empty()) return ByteRange{0, size - 1, false};
    if (!header.starts_with("bytes=")) return {};
    auto value = header.substr(6); auto dash = value.find('-');
    if (dash == std::string::npos) return {};
    auto a = value.substr(0, dash), b = value.substr(dash + 1);
    auto parse = [](const std::string &s, uint64_t &n) {
        if (s.empty()) return false;
        auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), n);
        return error == std::errc{} && end == s.data() + s.size();
    };
    uint64_t start = 0, end = size - 1, number = 0;
    if (a.empty()) {
        if (!parse(b, number) || !number) return {};
        start = number >= size ? 0 : size - number;
    } else {
        if (!parse(a, start) || (!b.empty() && !parse(b, number))) return {};
        if (!b.empty()) end = std::min(number, size - 1);
    }
    if (start >= size || start > end) return {};
    return ByteRange{start, end, true};
}
namespace {
// WebView2 reads IStream on its own threads. Each response owns a bounded,
// independently seekable file handle; large songs are never copied into RAM.
class FileSlice final : public IStream {
public:
    FileSlice(fs::path file, uint64_t offset, uint64_t length) : file_(std::move(file)), offset_(offset), length_(length) {
        handle_ = CreateFileW(file_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("无法读取本地音频文件");
    }
    ~FileSlice() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (id == IID_IUnknown || id == IID_ISequentialStream || id == IID_IStream) { *result = static_cast<IStream *>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override { auto left = InterlockedDecrement(&references_); if (!left) delete this; return left; }
    HRESULT STDMETHODCALLTYPE Read(void *buffer, ULONG count, ULONG *read) override {
        std::lock_guard lock(mutex_); if (read) *read = 0; if (!buffer && count) return STG_E_INVALIDPOINTER;
        auto remaining = position_ < length_ ? length_ - position_ : 0;
        DWORD wanted = static_cast<DWORD>(std::min<uint64_t>(count, remaining)), got = 0;
        LARGE_INTEGER where{}; where.QuadPart = static_cast<LONGLONG>(offset_ + position_);
        if (!SetFilePointerEx(handle_, where, nullptr, FILE_BEGIN) || !ReadFile(handle_, buffer, wanted, &got, nullptr)) return HRESULT_FROM_WIN32(GetLastError());
        position_ += got; if (read) *read = got; return got == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(const void *, ULONG, ULONG *) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER delta, DWORD origin, ULARGE_INTEGER *result) override {
        std::lock_guard lock(mutex_);
        if (origin > STREAM_SEEK_END) return STG_E_INVALIDFUNCTION;
        const auto base = static_cast<LONGLONG>(origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? position_ : length_);
        if ((delta.QuadPart < 0 && delta.QuadPart < -base) || (delta.QuadPart > 0 && base > LLONG_MAX - delta.QuadPart)) return STG_E_INVALIDFUNCTION;
        position_ = static_cast<uint64_t>(base + delta.QuadPart); if (result) result->QuadPart = position_; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream *destination, ULARGE_INTEGER count, ULARGE_INTEGER *read, ULARGE_INTEGER *written) override {
        if (!destination) return E_POINTER;
        if (read) read->QuadPart = 0; if (written) written->QuadPart = 0;
        uint64_t total = 0; char buffer[32768];
        while (total < count.QuadPart) {
            ULONG got = 0, sent = 0;
            auto hr = Read(buffer, static_cast<ULONG>(std::min<uint64_t>(sizeof(buffer), count.QuadPart - total)), &got);
            if (FAILED(hr)) return hr;
            if (!got) break;
            if (read) read->QuadPart += got;
            hr = destination->Write(buffer, got, &sent); if (written) written->QuadPart += sent;
            if (FAILED(hr)) return hr; if (sent != got) return STG_E_MEDIUMFULL; total += got;
        }
        return total == count.QuadPart ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG *stat, DWORD flags) override {
        if (!stat) return E_POINTER; *stat = {}; stat->type = STGTY_STREAM; stat->cbSize.QuadPart = length_; stat->grfMode = STGM_READ;
        if (!(flags & STATFLAG_NONAME)) { auto name = file_.filename().wstring(); stat->pwcsName = static_cast<LPOLESTR>(CoTaskMemAlloc((name.size() + 1) * sizeof(wchar_t))); if (!stat->pwcsName) return E_OUTOFMEMORY; std::memcpy(stat->pwcsName, name.c_str(), (name.size() + 1) * sizeof(wchar_t)); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream **result) override {
        if (!result) return E_POINTER; *result = nullptr; std::lock_guard lock(mutex_);
        try { auto copy = new FileSlice(file_, offset_, length_); copy->position_ = position_; *result = copy; return S_OK; } catch (...) { return STG_E_READFAULT; }
    }
private:
    LONG references_ = 1; HANDLE handle_ = INVALID_HANDLE_VALUE;
    fs::path file_; uint64_t offset_, length_, position_ = 0; std::mutex mutex_;
};
class MemorySlice final : public IStream {
public:
    MemorySlice(std::shared_ptr<const std::string> bytes, uint64_t offset, uint64_t length)
        : bytes_(std::move(bytes)), offset_(offset), length_(length) {
        if (!bytes_ || offset_ > bytes_->size() || length_ > bytes_->size() - offset_)
            throw std::runtime_error("无效的缓存范围");
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (id == IID_IUnknown || id == IID_ISequentialStream || id == IID_IStream) { *result = static_cast<IStream *>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override { auto left = InterlockedDecrement(&references_); if (!left) delete this; return left; }
    HRESULT STDMETHODCALLTYPE Read(void *buffer, ULONG count, ULONG *read) override {
        std::lock_guard lock(mutex_); if (read) *read = 0; if (!buffer && count) return STG_E_INVALIDPOINTER;
        const auto got = static_cast<ULONG>(std::min<uint64_t>(count, position_ < length_ ? length_ - position_ : 0));
        if (got) std::memcpy(buffer, bytes_->data() + offset_ + position_, got);
        position_ += got; if (read) *read = got; return got == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(const void *, ULONG, ULONG *) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER delta, DWORD origin, ULARGE_INTEGER *result) override {
        std::lock_guard lock(mutex_);
        if (origin > STREAM_SEEK_END) return STG_E_INVALIDFUNCTION;
        const auto base = static_cast<LONGLONG>(origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? position_ : length_);
        if ((delta.QuadPart < 0 && delta.QuadPart < -base) || (delta.QuadPart > 0 && base > LLONG_MAX - delta.QuadPart)) return STG_E_INVALIDFUNCTION;
        position_ = static_cast<uint64_t>(base + delta.QuadPart); if (result) result->QuadPart = position_; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream *destination, ULARGE_INTEGER count, ULARGE_INTEGER *read, ULARGE_INTEGER *written) override {
        if (!destination) return E_POINTER;
        if (read) read->QuadPart = 0; if (written) written->QuadPart = 0;
        uint64_t total = 0; char buffer[32768];
        while (total < count.QuadPart) {
            ULONG got = 0, sent = 0;
            auto hr = Read(buffer, static_cast<ULONG>(std::min<uint64_t>(sizeof(buffer), count.QuadPart - total)), &got);
            if (FAILED(hr)) return hr; if (!got) break;
            if (read) read->QuadPart += got;
            hr = destination->Write(buffer, got, &sent); if (written) written->QuadPart += sent;
            if (FAILED(hr)) return hr; if (sent != got) return STG_E_MEDIUMFULL; total += got;
        }
        return total == count.QuadPart ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG *stat, DWORD) override {
        if (!stat) return E_POINTER; *stat = {}; stat->type = STGTY_STREAM; stat->cbSize.QuadPart = length_; stat->grfMode = STGM_READ; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream **result) override {
        if (!result) return E_POINTER; *result = nullptr; std::lock_guard lock(mutex_);
        try { auto copy = new MemorySlice(bytes_, offset_, length_); copy->position_ = position_; *result = copy; return S_OK; } catch (...) { return E_OUTOFMEMORY; }
    }
private:
    LONG references_ = 1;
    std::shared_ptr<const std::string> bytes_;
    uint64_t offset_, length_, position_ = 0;
    std::mutex mutex_;
};
}
Microsoft::WRL::ComPtr<IStream> file_slice(const fs::path &file, uint64_t offset, uint64_t length) {
    Microsoft::WRL::ComPtr<IStream> result; result.Attach(new FileSlice(file, offset, length)); return result;
}
Microsoft::WRL::ComPtr<IStream> memory_slice(std::shared_ptr<const std::string> bytes, uint64_t offset, uint64_t length) {
    Microsoft::WRL::ComPtr<IStream> result; result.Attach(new MemorySlice(std::move(bytes), offset, length)); return result;
}
MediaResponse serve_media(const Library &library, const std::wstring &uri, const std::string &header, const std::string &method) {
    MediaResponse response;
    try {
        const std::wstring origin = L"https://media.dr-music.local/";
        if (!uri.starts_with(origin) || (method != "GET" && method != "HEAD")) return response;
        auto route = utf8(uri.substr(origin.size()));
        std::string revision;
        if (const auto query = route.find('?'); query != std::string::npos) {
            revision = route.substr(query + 1); route.resize(query);
            if (!revision.starts_with("v=")) return response;
            revision.erase(0, 2);
            const bool numeric = !revision.empty() && revision.size() <= 20 && revision.find_first_not_of("0123456789") == std::string::npos;
            const bool hash = revision.size() == 64 && revision.find_first_not_of("0123456789abcdef") == std::string::npos;
            if (!numeric && !hash) return response;
        }
        auto slash = route.find('/');
        if (slash == std::string::npos) return response;
        auto kind = route.substr(0, slash), id = route.substr(slash + 1);
        if ((kind != "audio" && kind != "cover") || id.size() != 24 || id.find_first_not_of("0123456789abcdef") != std::string::npos) return response;
        fs::path file; std::string mime;
        std::shared_ptr<const CoverResource> cover;
        uint64_t size = 0;
        if (kind == "cover") {
            cover = library.cover_resource(id, revision);
            if (!cover) return response;
            mime = cover->mime; size = cover->bytes.size();
        } else {
            if (!revision.empty()) return response;
            file = library.audio_path(id); size = fs::file_size(file);
            const std::map<std::string, std::string> types{{".mp3", "audio/mpeg"}, {".flac", "audio/flac"}, {".wav", "audio/wav"}, {".m4a", "audio/mp4"}, {".mp4", "audio/mp4"}, {".aac", "audio/aac"}, {".ogg", "audio/ogg"}, {".opus", "audio/ogg"}, {".webm", "audio/webm"}};
            auto ext = lower(path_text(file.extension())); mime = types.contains(ext) ? types.at(ext) : "application/octet-stream";
        }
        const auto range = byte_range(header, size);
        // Only content-addressed artwork is immutable. Numeric legacy URLs
        // may be reused after a library reset and must continue revalidating.
        const auto policy = cover ? (revision.size() == 64 && revision == cover->hash ? L"private, max-age=31536000, immutable" : L"no-cache") : L"no-store";
        response.headers = std::wstring(L"Cache-Control: ") + policy + L"\r\nAccess-Control-Allow-Origin: https://app.dr-music.local\r\nAccess-Control-Expose-Headers: Content-Range\r\nAccept-Ranges: bytes\r\n";
        if (cover && !cover->hash.empty()) response.headers += L"ETag: \"" + wide(cover->hash) + L"\"\r\n";
        if (!range) { response.status = 416; response.headers += L"Content-Range: bytes */" + std::to_wstring(size); return response; }
        const auto length = range->end - range->start + 1;
        response.status = range->partial ? 206 : 200;
        response.headers += L"Content-Type: " + wide(mime) + L"\r\nContent-Length: " + std::to_wstring(length);
        if (range->partial) response.headers += L"\r\nContent-Range: bytes " + std::to_wstring(range->start) + L"-" + std::to_wstring(range->end) + L"/" + std::to_wstring(size);
        if (method != "HEAD") response.stream = cover
            ? memory_slice(std::shared_ptr<const std::string>(cover, &cover->bytes), range->start, length)
            : file_slice(file, range->start, length);
    } catch (const std::exception &) { return MediaResponse{}; }
    return response;
}
}
