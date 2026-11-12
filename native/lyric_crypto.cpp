#include "lyric_crypto.h"
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <zlib.h>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace dr {
namespace {
struct Algorithm {
    BCRYPT_ALG_HANDLE handle = nullptr;
    explicit Algorithm(const wchar_t *id) { if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&handle, id, nullptr, 0))) throw std::runtime_error("加密组件不可用"); }
    ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
    Algorithm(const Algorithm &) = delete;
    Algorithm &operator=(const Algorithm &) = delete;
};

// ---- QQ Music QRC: a non-standard Triple-DES ----------------------------------------------------
// Ported line for line from Lyricify Lyrics Helper (Apache-2.0), Decrypter/Qrc/DESHelper.cs, which
// reproduces QQ Music's own implementation including its unusual bit order and S-box values.
// It is not interchangeable with a standard DES library.
namespace qq {
constexpr uint32_t ENCRYPT = 1, DECRYPT = 0;
using Schedule = std::array<std::array<uint8_t, 6>, 16>;
inline uint32_t BITNUM(const uint8_t *a, int b, int c) { return static_cast<uint32_t>((a[(b) / 32 * 4 + 3 - (b) % 32 / 8] >> (7 - (b % 8))) & 0x01) << (c); }
inline uint8_t BITNUMINTR(uint32_t a, int b, int c) { return static_cast<uint8_t>((((a) >> (31 - (b))) & 0x00000001) << (c)); }
inline uint32_t BITNUMINTL(uint32_t a, int b, int c) { return ((((a) << (b)) & 0x80000000) >> (c)); }
inline uint32_t SBOXBIT(uint8_t a) { return static_cast<uint32_t>(((a) & 0x20) | (((a) & 0x1f) >> 1) | (((a) & 0x01) << 4)); }
constexpr uint8_t sbox1[] = {14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7, 0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8, 4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0, 15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13};
constexpr uint8_t sbox2[] = {15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10, 3,13,4,7,15,2,8,15,12,0,1,10,6,9,11,5, 0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15, 13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9};
constexpr uint8_t sbox3[] = {10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8, 13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1, 13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7, 1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12};
constexpr uint8_t sbox4[] = {7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15, 13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9, 10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4, 3,15,0,6,10,10,13,8,9,4,5,11,12,7,2,14};
constexpr uint8_t sbox5[] = {2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9, 14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6, 4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14, 11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3};
constexpr uint8_t sbox6[] = {12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11, 10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8, 9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6, 4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13};
constexpr uint8_t sbox7[] = {4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1, 13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6, 1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2, 6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12};
constexpr uint8_t sbox8[] = {13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7, 1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2, 7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8, 2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11};

void KeySchedule(const uint8_t *key, Schedule &schedule, uint32_t mode) {
    static constexpr uint32_t key_rnd_shift[] = {1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1};
    static constexpr uint32_t key_perm_c[] = {56,48,40,32,24,16,8,0,57,49,41,33,25,17,9,1,58,50,42,34,26,18,10,2,59,51,43,35};
    static constexpr uint32_t key_perm_d[] = {62,54,46,38,30,22,14,6,61,53,45,37,29,21,13,5,60,52,44,36,28,20,12,4,27,19,11,3};
    static constexpr uint32_t key_compression[] = {13,16,10,23,0,4,2,27,14,5,20,9,22,18,11,3,25,7,15,6,26,19,12,1,
                                                   40,51,30,36,46,54,29,39,50,44,32,47,43,48,38,55,33,52,45,41,49,35,28,31};
    uint32_t i, j, toGen, C = 0, D = 0;
    for (i = 0, j = 31; i < 28; ++i, --j) C |= BITNUM(key, static_cast<int>(key_perm_c[i]), static_cast<int>(j));
    for (i = 0, j = 31; i < 28; ++i, --j) D |= BITNUM(key, static_cast<int>(key_perm_d[i]), static_cast<int>(j));
    for (i = 0; i < 16; ++i) {
        C = ((C << key_rnd_shift[i]) | (C >> (28 - key_rnd_shift[i]))) & 0xfffffff0;
        D = ((D << key_rnd_shift[i]) | (D >> (28 - key_rnd_shift[i]))) & 0xfffffff0;
        toGen = mode == DECRYPT ? 15 - i : i;
        for (j = 0; j < 6; ++j) schedule[toGen][j] = 0;
        for (j = 0; j < 24; ++j) schedule[toGen][j / 8] |= BITNUMINTR(C, static_cast<int>(key_compression[j]), static_cast<int>(7 - (j % 8)));
        for (; j < 48; ++j) schedule[toGen][j / 8] |= BITNUMINTR(D, static_cast<int>(key_compression[j]) - 27, static_cast<int>(7 - (j % 8)));
    }
}
void IP(uint32_t state[2], const uint8_t *in) {
    state[0] = BITNUM(in,57,31)|BITNUM(in,49,30)|BITNUM(in,41,29)|BITNUM(in,33,28)|BITNUM(in,25,27)|BITNUM(in,17,26)|BITNUM(in,9,25)|BITNUM(in,1,24)|
               BITNUM(in,59,23)|BITNUM(in,51,22)|BITNUM(in,43,21)|BITNUM(in,35,20)|BITNUM(in,27,19)|BITNUM(in,19,18)|BITNUM(in,11,17)|BITNUM(in,3,16)|
               BITNUM(in,61,15)|BITNUM(in,53,14)|BITNUM(in,45,13)|BITNUM(in,37,12)|BITNUM(in,29,11)|BITNUM(in,21,10)|BITNUM(in,13,9)|BITNUM(in,5,8)|
               BITNUM(in,63,7)|BITNUM(in,55,6)|BITNUM(in,47,5)|BITNUM(in,39,4)|BITNUM(in,31,3)|BITNUM(in,23,2)|BITNUM(in,15,1)|BITNUM(in,7,0);
    state[1] = BITNUM(in,56,31)|BITNUM(in,48,30)|BITNUM(in,40,29)|BITNUM(in,32,28)|BITNUM(in,24,27)|BITNUM(in,16,26)|BITNUM(in,8,25)|BITNUM(in,0,24)|
               BITNUM(in,58,23)|BITNUM(in,50,22)|BITNUM(in,42,21)|BITNUM(in,34,20)|BITNUM(in,26,19)|BITNUM(in,18,18)|BITNUM(in,10,17)|BITNUM(in,2,16)|
               BITNUM(in,60,15)|BITNUM(in,52,14)|BITNUM(in,44,13)|BITNUM(in,36,12)|BITNUM(in,28,11)|BITNUM(in,20,10)|BITNUM(in,12,9)|BITNUM(in,4,8)|
               BITNUM(in,62,7)|BITNUM(in,54,6)|BITNUM(in,46,5)|BITNUM(in,38,4)|BITNUM(in,30,3)|BITNUM(in,22,2)|BITNUM(in,14,1)|BITNUM(in,6,0);
}
void InvIP(const uint32_t state[2], uint8_t *out) {
    auto byte = [&](int a, int b, int c, int d) {
        return static_cast<uint8_t>(BITNUMINTR(state[1], a, 7) | BITNUMINTR(state[0], a, 6) | BITNUMINTR(state[1], b, 5) | BITNUMINTR(state[0], b, 4) |
                                    BITNUMINTR(state[1], c, 3) | BITNUMINTR(state[0], c, 2) | BITNUMINTR(state[1], d, 1) | BITNUMINTR(state[0], d, 0));
    };
    out[3] = byte(7, 15, 23, 31); out[2] = byte(6, 14, 22, 30); out[1] = byte(5, 13, 21, 29); out[0] = byte(4, 12, 20, 28);
    out[7] = byte(3, 11, 19, 27); out[6] = byte(2, 10, 18, 26); out[5] = byte(1, 9, 17, 25); out[4] = byte(0, 8, 16, 24);
}
uint32_t F(uint32_t state, const std::array<uint8_t, 6> &key) {
    uint8_t lrgstate[6];
    const uint32_t t1 = BITNUMINTL(state,31,0)|((state&0xf0000000)>>1)|BITNUMINTL(state,4,5)|BITNUMINTL(state,3,6)|((state&0x0f000000)>>3)|BITNUMINTL(state,8,11)|
                        BITNUMINTL(state,7,12)|((state&0x00f00000)>>5)|BITNUMINTL(state,12,17)|BITNUMINTL(state,11,18)|((state&0x000f0000)>>7)|BITNUMINTL(state,16,23);
    const uint32_t t2 = BITNUMINTL(state,15,0)|((state&0x0000f000)<<15)|BITNUMINTL(state,20,5)|BITNUMINTL(state,19,6)|((state&0x00000f00)<<13)|BITNUMINTL(state,24,11)|
                        BITNUMINTL(state,23,12)|((state&0x000000f0)<<11)|BITNUMINTL(state,28,17)|BITNUMINTL(state,27,18)|((state&0x0000000f)<<9)|BITNUMINTL(state,0,23);
    lrgstate[0] = static_cast<uint8_t>((t1 >> 24) & 0xff); lrgstate[1] = static_cast<uint8_t>((t1 >> 16) & 0xff); lrgstate[2] = static_cast<uint8_t>((t1 >> 8) & 0xff);
    lrgstate[3] = static_cast<uint8_t>((t2 >> 24) & 0xff); lrgstate[4] = static_cast<uint8_t>((t2 >> 16) & 0xff); lrgstate[5] = static_cast<uint8_t>((t2 >> 8) & 0xff);
    for (int i = 0; i < 6; ++i) lrgstate[i] ^= key[i];
    state = (static_cast<uint32_t>(sbox1[SBOXBIT(static_cast<uint8_t>(lrgstate[0] >> 2))]) << 28) |
            (static_cast<uint32_t>(sbox2[SBOXBIT(static_cast<uint8_t>(((lrgstate[0] & 0x03) << 4) | (lrgstate[1] >> 4)))]) << 24) |
            (static_cast<uint32_t>(sbox3[SBOXBIT(static_cast<uint8_t>(((lrgstate[1] & 0x0f) << 2) | (lrgstate[2] >> 6)))]) << 20) |
            (static_cast<uint32_t>(sbox4[SBOXBIT(static_cast<uint8_t>(lrgstate[2] & 0x3f))]) << 16) |
            (static_cast<uint32_t>(sbox5[SBOXBIT(static_cast<uint8_t>(lrgstate[3] >> 2))]) << 12) |
            (static_cast<uint32_t>(sbox6[SBOXBIT(static_cast<uint8_t>(((lrgstate[3] & 0x03) << 4) | (lrgstate[4] >> 4)))]) << 8) |
            (static_cast<uint32_t>(sbox7[SBOXBIT(static_cast<uint8_t>(((lrgstate[4] & 0x0f) << 2) | (lrgstate[5] >> 6)))]) << 4) |
            static_cast<uint32_t>(sbox8[SBOXBIT(static_cast<uint8_t>(lrgstate[5] & 0x3f))]);
    return BITNUMINTL(state,15,0)|BITNUMINTL(state,6,1)|BITNUMINTL(state,19,2)|BITNUMINTL(state,20,3)|BITNUMINTL(state,28,4)|BITNUMINTL(state,11,5)|
           BITNUMINTL(state,27,6)|BITNUMINTL(state,16,7)|BITNUMINTL(state,0,8)|BITNUMINTL(state,14,9)|BITNUMINTL(state,22,10)|BITNUMINTL(state,25,11)|
           BITNUMINTL(state,4,12)|BITNUMINTL(state,17,13)|BITNUMINTL(state,30,14)|BITNUMINTL(state,9,15)|BITNUMINTL(state,1,16)|BITNUMINTL(state,7,17)|
           BITNUMINTL(state,23,18)|BITNUMINTL(state,13,19)|BITNUMINTL(state,31,20)|BITNUMINTL(state,26,21)|BITNUMINTL(state,2,22)|BITNUMINTL(state,8,23)|
           BITNUMINTL(state,18,24)|BITNUMINTL(state,12,25)|BITNUMINTL(state,29,26)|BITNUMINTL(state,5,27)|BITNUMINTL(state,21,28)|BITNUMINTL(state,10,29)|
           BITNUMINTL(state,3,30)|BITNUMINTL(state,24,31);
}
void Crypt(const uint8_t *in, uint8_t *out, const Schedule &key) {
    uint32_t state[2];
    IP(state, in);
    for (int idx = 0; idx < 15; ++idx) { const uint32_t t = state[1]; state[1] = F(state[1], key[idx]) ^ state[0]; state[0] = t; }
    state[0] = F(state[1], key[15]) ^ state[0];
    InvIP(state, out);
}
void TripleDESKeySetup(const uint8_t *key, std::array<Schedule, 3> &schedule, uint32_t mode) {
    if (mode == ENCRYPT) { KeySchedule(key, schedule[0], mode); KeySchedule(key + 8, schedule[1], DECRYPT); KeySchedule(key + 16, schedule[2], mode); }
    else { KeySchedule(key, schedule[2], mode); KeySchedule(key + 8, schedule[1], ENCRYPT); KeySchedule(key + 16, schedule[0], mode); }
}
void TripleDESCrypt(const uint8_t *in, uint8_t *out, const std::array<Schedule, 3> &key) {
    uint8_t a[8], b[8];
    Crypt(in, a, key[0]); Crypt(a, b, key[1]); Crypt(b, out, key[2]);
}
constexpr char kQrcKey[] = "!@#)(*$%123ZXC!@!@#)(NHL";
std::string run(std::string data, uint32_t mode) {
    std::array<Schedule, 3> schedule{};
    TripleDESKeySetup(reinterpret_cast<const uint8_t *>(kQrcKey), schedule, mode);
    for (size_t i = 0; i + 8 <= data.size(); i += 8) {
        uint8_t out[8];
        TripleDESCrypt(reinterpret_cast<const uint8_t *>(data.data() + i), out, schedule);
        std::copy(out, out + 8, data.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return data;
}
}
}

std::string md5_hex(std::string_view data) {
    Algorithm md5(BCRYPT_MD5_ALGORITHM);
    unsigned char digest[16];
    if (!BCRYPT_SUCCESS(BCryptHash(md5.handle, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char *>(data.data())), static_cast<ULONG>(data.size()), digest, sizeof(digest))))
        throw std::runtime_error("MD5 失败");
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (auto b : digest) { out += hex[b >> 4]; out += hex[b & 15]; }
    return out;
}

std::string aes128_ecb_encrypt(std::string_view data, std::string_view key) {
    if (key.size() != 16) throw std::invalid_argument("AES-128 needs a 16-byte key");
    Algorithm aes(BCRYPT_AES_ALGORITHM);
    BCryptSetProperty(aes.handle, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_ECB)), sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    BCRYPT_KEY_HANDLE handle = nullptr;
    if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(aes.handle, &handle, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char *>(key.data())), 16, 0))) throw std::runtime_error("AES 失败");
    ULONG size = 0;
    auto input = reinterpret_cast<PUCHAR>(const_cast<char *>(data.data()));
    BCryptEncrypt(handle, input, static_cast<ULONG>(data.size()), nullptr, nullptr, 0, nullptr, 0, &size, BCRYPT_BLOCK_PADDING);
    std::string out(size, '\0');
    const auto status = BCryptEncrypt(handle, input, static_cast<ULONG>(data.size()), nullptr, nullptr, 0, reinterpret_cast<PUCHAR>(out.data()), size, &size, BCRYPT_BLOCK_PADDING);
    BCryptDestroyKey(handle);
    if (!BCRYPT_SUCCESS(status)) throw std::runtime_error("AES 失败");
    out.resize(size);
    return out;
}

std::string hex_upper(std::string_view data) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char b : data) { out += hex[b >> 4]; out += hex[b & 15]; }
    return out;
}

std::string base64_decode(std::string_view text) {
    DWORD size = 0;
    if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr)) throw std::runtime_error("Base64 数据无效");
    std::string out(size, '\0');
    if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, reinterpret_cast<BYTE *>(out.data()), &size, nullptr, nullptr)) throw std::runtime_error("Base64 数据无效");
    out.resize(size);
    return out;
}

std::string base64_encode(std::string_view data) {
    DWORD size = 0;
    const auto flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    CryptBinaryToStringA(reinterpret_cast<const BYTE *>(data.data()), static_cast<DWORD>(data.size()), flags, nullptr, &size);
    std::string out(size, '\0');
    CryptBinaryToStringA(reinterpret_cast<const BYTE *>(data.data()), static_cast<DWORD>(data.size()), flags, out.data(), &size);
    out.resize(size);
    return out;
}

std::string inflate_zlib(std::string_view data, size_t limit) {
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) throw std::runtime_error("解压失败");
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    std::string out;
    char buffer[16384];
    int status = Z_OK;
    while (status != Z_STREAM_END) {
        stream.next_out = reinterpret_cast<Bytef *>(buffer); stream.avail_out = sizeof(buffer);
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) { inflateEnd(&stream); throw std::runtime_error("歌词数据损坏"); }
        out.append(buffer, sizeof(buffer) - stream.avail_out);
        if (out.size() > limit) { inflateEnd(&stream); throw std::runtime_error("歌词数据过大"); }
        if (status == Z_OK && stream.avail_in == 0 && stream.avail_out != 0) { inflateEnd(&stream); throw std::runtime_error("歌词数据不完整"); }
    }
    inflateEnd(&stream);
    return out;
}

std::string decrypt_krc(std::string_view base64) {
    // From Lyricify Lyrics Helper (Apache-2.0), Decrypter/Krc/Decrypter.cs.
    static constexpr unsigned char key[] = {0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47, 0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69};
    auto data = base64_decode(base64);
    if (data.size() <= 4) throw std::runtime_error("KRC 数据无效");
    data.erase(0, 4);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>(static_cast<unsigned char>(data[i]) ^ key[i % sizeof(key)]);
    auto text = inflate_zlib(data);
    if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3); // Lyricify drops this leading BOM
    return text;
}

std::string decrypt_qrc(std::string_view hex) {
    if (hex.size() % 16 || hex.empty()) throw std::runtime_error("QRC 数据无效");
    std::string data(hex.size() / 2, '\0');
    for (size_t i = 0; i < data.size(); ++i) {
        auto nibble = [&](char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            throw std::runtime_error("QRC 数据无效");
        };
        data[i] = static_cast<char>(nibble(hex[i * 2]) << 4 | nibble(hex[i * 2 + 1]));
    }
    auto text = inflate_zlib(qq::run(std::move(data), qq::DECRYPT));
    if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
    return text;
}

std::string encrypt_qrc(std::string_view text) {
    uLongf size = compressBound(static_cast<uLong>(text.size()));
    std::string compressed(size, '\0');
    if (compress(reinterpret_cast<Bytef *>(compressed.data()), &size, reinterpret_cast<const Bytef *>(text.data()), static_cast<uLong>(text.size())) != Z_OK) throw std::runtime_error("压缩失败");
    compressed.resize(size);
    compressed.resize((compressed.size() + 7) / 8 * 8, '\0');
    const auto encrypted = qq::run(std::move(compressed), qq::ENCRYPT);
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (unsigned char b : encrypted) { out += hex[b >> 4]; out += hex[b & 15]; }
    return out;
}
}
