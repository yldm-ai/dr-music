#pragma once
#include "common.h"
#include <functional>
#include <string>
#include <string_view>
namespace dr {
// Codecs used by the online lyric providers. MD5 / AES come from Windows CNG; inflate from zlib.
/** SHA-256 of a file as lowercase hex, reading it in chunks; `progress` gets bytes read / total. */
std::string sha256_file_hex(const fs::path &file, const std::function<void(uint64_t, uint64_t)> &progress = {});
std::string md5_hex(std::string_view data);
/** AES-128-ECB with PKCS#7 padding (NetEase EAPI). */
std::string aes128_ecb_encrypt(std::string_view data, std::string_view key);
std::string hex_upper(std::string_view data);
std::string base64_decode(std::string_view text);
std::string base64_encode(std::string_view data);
/** zlib stream (RFC 1950) inflate, size-capped. */
std::string inflate_zlib(std::string_view data, size_t limit = 16 * 1024 * 1024);
/** Kugou KRC: base64 → skip "krc1" header → XOR key → zlib. Returns the KRC text. */
std::string decrypt_krc(std::string_view base64);
/** QQ Music QRC: hex → QQ's Triple-DES variant → zlib. Returns the (XML or QRC) text. */
std::string decrypt_qrc(std::string_view hex);
/** Inverse of decrypt_qrc for tests: zlib-compressed text, encrypted, as lower-case hex. */
std::string encrypt_qrc(std::string_view text);
}
