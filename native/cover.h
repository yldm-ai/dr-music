#pragma once
#include "common.h"
#include "cache.h"
#include <map>
#include <vector>
#include <optional>
#include <string_view>
namespace TagLib { class FileRef; }
namespace dr {
inline constexpr unsigned metadata_parser_version = 2;
struct CoverImage {
    std::string bytes, mime, source;
    unsigned width = 0, height = 0;
};
struct CoverResult {
    std::shared_ptr<const CoverImage> image;
    std::string source, error;
};
struct CoverFile { fs::path path; std::string stamp; };
struct CoverInputs { std::vector<CoverFile> files; std::string fingerprint; bool valid = true; };
class CoverCache {
public:
    CoverInputs inputs(const fs::path &audio);
    CoverResult find(const CoverInputs &inputs, const TagLib::FileRef *tags);
    std::shared_ptr<const CoverImage> decode(std::string_view bytes);
    void clear_directories() { directories_.clear(); }
    void clear() { directories_.clear(); files_.clear(); images_.clear(); }
private:
    using Directory = std::map<std::string, CoverFile>;
    BoundedCache<Directory> directories_{4 * 1024 * 1024, 128};
    BoundedCache<std::string> files_{16 * 1024 * 1024, 128};
    BoundedCache<CoverImage> images_{24 * 1024 * 1024, 256};
};
// All decoding and resizing happens on the native metadata worker. MIME hints
// from tags and filename extensions are deliberately not trusted.
std::optional<CoverImage> decode_cover(std::string_view bytes, const std::string &source = {});
}
