#pragma once
#include "library.h"
#include <objidl.h>
#include <optional>
#include <wrl.h>
namespace dr {
struct ByteRange { uint64_t start, end; bool partial; };
std::optional<ByteRange> byte_range(const std::string &header, uint64_t size);
Microsoft::WRL::ComPtr<IStream> file_slice(const fs::path &file, uint64_t offset, uint64_t length);
Microsoft::WRL::ComPtr<IStream> memory_slice(std::shared_ptr<const std::string> bytes, uint64_t offset, uint64_t length);
struct MediaResponse {
    int status = 404;
    std::wstring headers = L"Content-Type: text/plain\r\nCache-Control: no-store";
    Microsoft::WRL::ComPtr<IStream> stream;
};
MediaResponse serve_media(const Library &library, const std::wstring &uri, const std::string &range, const std::string &method);
}
