#pragma once
#include <windows.h>
#include <objbase.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <optional>
#include <nlohmann/json.hpp>
namespace dr {
namespace fs = std::filesystem;
using Json = nlohmann::json;
std::string utf8(const std::wstring &text);
std::wstring wide(const std::string &text);
std::string path_text(const fs::path &path);
fs::path from_text(const std::string &text);
std::wstring take(wchar_t *value);
std::string read_text(const fs::path &file, size_t limit = 64 * 1024 * 1024);
void write_atomic(const fs::path &file, const std::string &text);
std::string lower(std::string text);
std::string path_key(const fs::path &file);
// Normalize paths already stored by the importer without querying the disk.
std::string stored_path_key(const fs::path &file);
std::string track_id(const fs::path &file);
std::string content_hash(std::string_view bytes);
std::optional<std::string> file_stamp(const fs::path &file);
long long timestamp();
}
