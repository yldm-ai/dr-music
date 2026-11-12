#pragma once
#include "common.h"
#include <optional>
#include <string_view>
namespace TagLib { class FileRef; }
namespace dr {
inline constexpr unsigned metadata_parser_version = 1;
struct CoverImage {
    std::string bytes, mime, source;
    unsigned width = 0, height = 0;
};
struct CoverResult {
    std::optional<CoverImage> image;
    std::string error;
};
// All decoding and resizing happens on the native metadata worker. MIME hints
// from tags and filename extensions are deliberately not trusted.
std::optional<CoverImage> decode_cover(std::string_view bytes, const std::string &source = {});
CoverResult find_cover(const fs::path &audio, const TagLib::FileRef *tags);
}
