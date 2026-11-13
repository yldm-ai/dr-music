#include "common.h"
#include <bcrypt.h>
#include <chrono>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <vector>
namespace dr {
std::string utf8(const std::wstring &text) {
    if (text.empty()) return {};
    auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("无效的 Unicode 文本");
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}
std::wstring wide(const std::string &text) {
    if (text.empty()) return {};
    auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size) throw std::runtime_error("无效的 UTF-8 文本，请将文件转换为 UTF-8 或带 BOM 的 UTF-16LE");
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}
std::string path_text(const fs::path &p) { return utf8(p.wstring()); }
fs::path from_text(const std::string &text) { return fs::path(wide(text)); }
std::wstring take(wchar_t *value) { std::wstring text = value ? value : L""; CoTaskMemFree(value); return text; }
std::string read_bytes(const fs::path &file, size_t limit) {
    const auto size = fs::file_size(file);
    if (size > limit) throw std::runtime_error("文件超过读取上限：" + path_text(file));
    std::ifstream in(file, std::ios::binary);
    std::string bytes(static_cast<size_t>(size), '\0');
    if (!in || (size && !in.read(bytes.data(), static_cast<std::streamsize>(size)))) throw std::runtime_error("无法读取文件：" + path_text(file));
    return bytes;
}
std::string read_text(const fs::path &file, size_t limit) {
    auto size = fs::file_size(file);
    if (size > limit) throw std::runtime_error("文件超过读取上限：" + path_text(file));
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("无法读取文件：" + path_text(file));
    std::string bytes(static_cast<size_t>(size), '\0');
    if (size && !in.read(bytes.data(), static_cast<std::streamsize>(size))) throw std::runtime_error("文件读取不完整");
    if (bytes.starts_with("\xff\xfe")) {
        if (bytes.size() % 2) throw std::runtime_error("UTF-16LE 文件长度无效");
        std::wstring text;
        for (size_t i = 2; i < bytes.size(); i += 2) text.push_back(static_cast<wchar_t>(static_cast<unsigned char>(bytes[i]) | (static_cast<unsigned char>(bytes[i + 1]) << 8)));
        return utf8(text);
    }
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.erase(0, 3);
    (void)wide(bytes); return bytes;
}
void write_atomic(const fs::path &file, const std::string &text) {
    auto temp = file; temp += L".tmp";
    { std::ofstream out(temp, std::ios::binary | std::ios::trunc); out.write(text.data(), static_cast<std::streamsize>(text.size())); out.flush(); if (!out) throw std::runtime_error("无法保存文件：" + path_text(file)); }
    if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("无法替换音乐库文件");
}
std::string lower(std::string text) { std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return text; }
std::string path_key(const fs::path &file) {
    return stored_path_key(fs::weakly_canonical(file));
}
std::string stored_path_key(const fs::path &file) {
    auto text = file.lexically_normal().wstring();
    if (text.starts_with(L"\\\\?\\")) text.erase(0, 4);
    std::replace(text.begin(), text.end(), L'/', L'\\');
    while (text.size() > 3 && text.back() == L'\\') text.pop_back();
    std::wstring normalized(text.size(), L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), static_cast<int>(text.size()), normalized.data(), static_cast<int>(normalized.size()), nullptr, nullptr, 0)) normalized = text;
    return utf8(normalized);
}
std::string track_id(const fs::path &file) {
    auto key = stored_path_key(file); unsigned char hash[32]{};
    auto status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), hash, sizeof(hash));
    if (status < 0) throw std::runtime_error("无法生成歌曲标识");
    std::ostringstream out; out << std::hex << std::setfill('0');
    for (int i = 0; i < 12; ++i) out << std::setw(2) << static_cast<int>(hash[i]);
    return out.str();
}
long long timestamp() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::string content_hash(std::string_view bytes) {
    unsigned char hash[32]{};
    if (bytes.size() > ULONG_MAX || BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char *>(bytes.data())), static_cast<ULONG>(bytes.size()), hash, sizeof(hash)) < 0)
        throw std::runtime_error("无法计算缓存标识");
    std::ostringstream out; out << std::hex << std::setfill('0');
    for (auto byte : hash) out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}
std::optional<std::string> file_stamp(const fs::path &file) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return {};
    auto time = [](FILETIME value) { return (static_cast<unsigned long long>(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
    const auto size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return std::to_string(size) + ":" + std::to_string(time(data.ftLastWriteTime)) + ":" + std::to_string(time(data.ftCreationTime));
}
}
