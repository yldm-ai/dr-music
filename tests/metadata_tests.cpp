#include "cover.h"
#include "library.h"
#include "media.h"
#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>
#include <tvariant.h>
#include <taglib.h>
#include <zlib.h>
#include <wincodec.h>
#include <wrl.h>
#include <array>
#include <fstream>
#include <iostream>
#include <cstring>
#define CHECK(value) do { if (!(value)) throw std::runtime_error("Metadata check failed at line " + std::to_string(__LINE__) + ": " #value); } while (false)
namespace {
void write(const dr::fs::path &file, const std::string &bytes) {
    std::ofstream out(file, std::ios::binary); out.write(bytes.data(), bytes.size()); CHECK(out.good());
}
void number(std::string &bytes, size_t offset, unsigned value, unsigned count, bool big = false) {
    for (unsigned i = 0; i < count; ++i) bytes[offset + i] = static_cast<char>(value >> (8 * (big ? count - i - 1 : i)));
}
std::string wave() {
    constexpr unsigned size = 22050 * 2;
    std::string bytes(44 + size, '\0');
    bytes.replace(0, 4, "RIFF"); number(bytes, 4, size + 36, 4); bytes.replace(8, 8, "WAVEfmt ");
    number(bytes, 16, 16, 4); number(bytes, 20, 1, 2); number(bytes, 22, 1, 2);
    number(bytes, 24, 22050, 4); number(bytes, 28, 44100, 4); number(bytes, 32, 2, 2); number(bytes, 34, 16, 2);
    bytes.replace(36, 4, "data"); number(bytes, 40, size, 4); return bytes;
}
std::string bitmap(unsigned width, unsigned height, unsigned rgb) {
    const unsigned stride = (width * 3 + 3) & ~3U;
    std::string bytes(54 + stride * height, '\0');
    bytes.replace(0, 2, "BM"); number(bytes, 2, static_cast<unsigned>(bytes.size()), 4);
    number(bytes, 10, 54, 4); number(bytes, 14, 40, 4); number(bytes, 18, width, 4); number(bytes, 22, height, 4);
    number(bytes, 26, 1, 2); number(bytes, 28, 24, 2); number(bytes, 34, stride * height, 4);
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) number(bytes, 54 + y * stride + x * 3, rgb, 3);
    return bytes;
}
unsigned pixel(const std::string &bytes) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory; CHECK(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))));
    ComPtr<IWICStream> stream; CHECK(SUCCEEDED(factory->CreateStream(&stream)));
    CHECK(SUCCEEDED(stream->InitializeFromMemory(reinterpret_cast<BYTE *>(const_cast<char *>(bytes.data())), static_cast<DWORD>(bytes.size()))));
    ComPtr<IWICBitmapDecoder> decoder; CHECK(SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)));
    ComPtr<IWICBitmapFrameDecode> frame; CHECK(SUCCEEDED(decoder->GetFrame(0, &frame)));
    ComPtr<IWICFormatConverter> converter; CHECK(SUCCEEDED(factory->CreateFormatConverter(&converter)));
    CHECK(SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)));
    BYTE rgba[4]{}; WICRect rect{0, 0, 1, 1}; CHECK(SUCCEEDED(converter->CopyPixels(&rect, 4, 4, rgba)));
    return (rgba[2] << 16) | (rgba[1] << 8) | rgba[0];
}
TagLib::VariantMap picture(const std::string &bytes, const char *type, const char *mime) {
    TagLib::VariantMap result;
    result.insert("data", TagLib::ByteVector(bytes.data(), static_cast<unsigned>(bytes.size())));
    result.insert("pictureType", TagLib::String(type)); result.insert("mimeType", TagLib::String(mime));
    return result;
}
void pictures(const dr::fs::path &file, const TagLib::List<TagLib::VariantMap> &images) {
    TagLib::FileRef ref(file.c_str()); CHECK(!ref.isNull());
    CHECK(ref.setComplexProperties("PICTURE", images)); CHECK(ref.save());
}
dr::Json enrich(dr::Library &library, const dr::fs::path &file) {
    const auto result = library.import_paths({file});
    CHECK(result["pending"].size() == 1);
    auto track = library.enrich_metadata(result["pending"][0]); CHECK(!track.is_null()); library.flush_metadata(); return track;
}
// Binary covers are not text; read them without the library's UTF decoder.
std::string cover_bytes(const dr::Library &library, const dr::Json &track) {
    std::ifstream input(library.directory() / (track["id"].get<std::string>() + ".cover"), std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void image_test() {
    const auto image = dr::decode_cover(bitmap(2048, 1024, 0x123456));
    CHECK(image); CHECK(image->mime == "image/png"); CHECK(image->width == 1024); CHECK(image->height == 512);
    CHECK(image->bytes.starts_with(std::string("\x89PNG\r\n\x1a\n", 8))); CHECK(pixel(image->bytes) == 0x123456);
    CHECK(!dr::decode_cover("not an image"));
    auto malformed = bitmap(2, 2, 0); number(malformed, 18, 0x7fffffff, 4); CHECK(!dr::decode_cover(malformed));
}
void embedded_test(const dr::fs::path &root) {
    const auto file = root / L"嵌入封面.wav"; write(file, wave());
    const auto red = bitmap(3, 2, 0xff0000), green = bitmap(3, 2, 0x00ff00);
    // MIME strings may be empty or wrong, and the first front cover may be
    // corrupt. A later valid front cover wins over a valid back cover.
    pictures(file, {picture(red, "Back Cover", "image/bmp"), picture("broken", "Front Cover", "image/png"), picture(green, "Front Cover", "image/jpeg")});
    {
        TagLib::FileRef ref(file.c_str());
        auto p = ref.properties(); p["TITLE"] = {TagLib::String(" 中文标题 ", TagLib::String::UTF8)}; p["ARTIST"] = {" Artist A ", "Artist B"};
        p["ALBUMARTIST"] = {"Album Artist"}; p["TRACKNUMBER"] = {"3/12"}; p["DISCNUMBER"] = {"2/3"}; p["DATE"] = {"2024-09-12"};
        ref.setProperties(p); CHECK(ref.save());
    }
    dr::Library library(root / L"embedded-data"); library.load(); const auto track = enrich(library, file);
    CHECK(track["title"] == "中文标题"); CHECK(track["artist"] == "Artist A / Artist B");
    CHECK(track["albumArtist"] == "Album Artist"); CHECK(track["trackNumber"] == 3); CHECK(track["discNumber"] == 2); CHECK(track["year"] == 2024);
    CHECK(track["cover"] == "image/png"); CHECK(track["coverSource"] == "embedded"); CHECK(!track.contains("coverError"));
    CHECK(pixel(cover_bytes(library, track)) == 0x00ff00);
    const auto revision = track["coverRevision"].get<unsigned long long>();
    const auto uri = L"https://media.dr-music.local/cover/" + dr::wide(track["id"].get<std::string>());
    CHECK(dr::serve_media(library, uri + L"?v=" + std::to_wstring(revision), "", "GET").status == 200);
    CHECK(dr::serve_media(library, uri + L"?v=0", "", "GET").status == 404);
    CHECK(dr::serve_media(library, uri + L"?v=../bad", "", "GET").status == 404);
    const auto old_bytes = cover_bytes(library, track);
    pictures(file, {picture(red, "Front Cover", "")});
    // A failed cache write keeps the previous good cover and its revision.
    const auto temp = library.directory() / (track["id"].get<std::string>() + ".cover.tmp");
    dr::fs::create_directory(temp);
    const auto failed = enrich(library, file);
    CHECK(failed.contains("coverError")); CHECK(failed["coverRevision"] == track["coverRevision"]);
    CHECK(cover_bytes(library, failed) == old_bytes); dr::fs::remove(temp);
    const auto repaired = enrich(library, file);
    CHECK(!repaired.contains("coverError")); CHECK(repaired["coverRevision"] != track["coverRevision"]);
    CHECK(pixel(cover_bytes(library, repaired)) == 0xff0000);
    CHECK(dr::serve_media(library, uri + L"?v=" + std::to_wstring(revision), "", "GET").status == 404);
    pictures(file, {});
    const auto cleared = enrich(library, file);
    CHECK(!cleared.contains("cover")); CHECK(!cleared.contains("coverRevision"));
    CHECK(!dr::fs::exists(library.directory() / (track["id"].get<std::string>() + ".cover")));
}
void external_test(const dr::fs::path &root) {
    const auto directory = root / L"external"; dr::fs::create_directory(directory);
    const auto file = directory / L"fallback.wav"; write(file, wave());
    pictures(file, {picture("invalid", "Front Cover", "image/jpeg")});
    write(directory / L"fallback.jpg", "invalid external cover");
    write(directory / L"folder.jpg", bitmap(4, 3, 0x334455));
    dr::Library library(root / L"external-data"); library.load(); auto track = enrich(library, file);
    CHECK(track["coverSource"] == "folder.jpg"); CHECK(pixel(cover_bytes(library, track)) == 0x334455);
    write(directory / L"fallback.jpg", bitmap(2, 2, 0xabcdef));
    track = enrich(library, file); CHECK(track["coverSource"] == "fallback.jpg"); CHECK(pixel(cover_bytes(library, track)) == 0xabcdef);
    const auto invalid_audio = directory / L"disappeared.wav"; write(invalid_audio, wave());
    const auto pending = library.import_paths({invalid_audio});
    dr::fs::remove(invalid_audio); // Simulate an unavailable file between discovery and enrichment.
    const auto invalid = library.enrich_metadata(pending["pending"][0]);
    CHECK(invalid.contains("metadataError")); CHECK(invalid["coverSource"] == "folder.jpg");
    CHECK(dr::fs::exists(file)); CHECK(dr::fs::exists(directory / L"folder.jpg"));
}
void append_frame(std::string &frames, const std::string &id, const std::string &data, bool compress) {
    std::string payload = data;
    if (compress) {
        uLongf size = compressBound(static_cast<uLong>(data.size()));
        std::string compressed(size, '\0');
        CHECK(compress2(reinterpret_cast<Bytef *>(compressed.data()), &size, reinterpret_cast<const Bytef *>(data.data()), static_cast<uLong>(data.size()), Z_BEST_COMPRESSION) == Z_OK);
        compressed.resize(size); payload.assign(4, '\0'); number(payload, 0, static_cast<unsigned>(data.size()), 4, true); payload += compressed;
    }
    std::string header = id + std::string(6, '\0');
    number(header, 4, static_cast<unsigned>(payload.size()), 4, true); if (compress) header[9] = static_cast<char>(0x80);
    frames += header + payload;
}
void compressed_test(const dr::fs::path &root) {
    const auto image = dr::decode_cover(bitmap(2, 2, 0x556677)); CHECK(image);
    std::string frames;
    append_frame(frames, "TIT2", std::string("\0Compressed title", 17), true);
    std::string apic(1, '\0'); apic += "image/png"; apic += '\0'; apic += '\x03'; apic += '\0'; apic += image->bytes;
    append_frame(frames, "APIC", apic, true);
    std::string id3("ID3\x03\x00\x00", 6); id3.resize(10);
    for (unsigned i = 0; i < 4; ++i) id3[6 + i] = static_cast<char>((frames.size() >> (7 * (3 - i))) & 0x7f);
    id3 += frames;
    auto audio = wave(); const auto offset = audio.size(); audio += "ID3 "; audio.resize(audio.size() + 4);
    number(audio, offset + 4, static_cast<unsigned>(id3.size()), 4); audio += id3;
    if (id3.size() & 1) audio += '\0'; number(audio, 4, static_cast<unsigned>(audio.size() - 8), 4);
    const auto file = root / L"compressed.wav"; write(file, audio);
    dr::Library library(root / L"compressed-data"); library.load(); const auto track = enrich(library, file);
    CHECK(track["title"] == "Compressed title"); CHECK(track["coverSource"] == "embedded"); CHECK(pixel(cover_bytes(library, track)) == 0x556677);
    // Generated MPEG-1 Layer III frames exercise the MP3 parser too.
    std::string frame(417, '\0'); frame.replace(0, 4, std::string("\xff\xfb\x90\x00", 4));
    const auto mp3 = root / L"compressed.mp3"; write(mp3, id3 + frame + frame + frame);
    const auto mpeg = enrich(library, mp3);
    CHECK(!mpeg.contains("metadataError")); CHECK(mpeg["title"] == "Compressed title");
    CHECK(mpeg["coverSource"] == "embedded"); CHECK(pixel(cover_bytes(library, mpeg)) == 0x556677);
}
void flac_test(const dr::fs::path &root) {
    // A generated STREAMINFO metadata block; no decoder or external fixture.
    std::string flac("fLaC\x80\x00\x00\x22", 8); flac.resize(42, '\0');
    number(flac, 8, 4096, 2, true); number(flac, 10, 4096, 2, true);
    const unsigned long long info = (44100ULL << 44) | (1ULL << 41) | (15ULL << 36) | 44100;
    for (unsigned i = 0; i < 8; ++i) flac[18 + i] = static_cast<char>(info >> (8 * (7 - i)));
    const auto file = root / L"generated.flac"; write(file, flac);
    pictures(file, {picture(bitmap(2, 2, 0x778899), "Front Cover", "image/bmp")});
    {
        TagLib::FileRef ref(file.c_str()); auto properties = ref.properties();
        properties["TITLE"] = {"FLAC title"}; properties["ARTIST"] = {"First", "Second"};
        ref.setProperties(properties); CHECK(ref.save());
    }
    dr::Library library(root / L"flac-data"); library.load(); const auto track = enrich(library, file);
    CHECK(!track.contains("metadataError")); CHECK(track["title"] == "FLAC title"); CHECK(track["artist"] == "First / Second");
    CHECK(track["coverSource"] == "embedded"); CHECK(pixel(cover_bytes(library, track)) == 0x778899);
}
void migration_test(const dr::fs::path &root) {
    const auto file = root / L"legacy.wav"; write(file, wave());
    dr::Library library(root / L"legacy-data"); library.load(); const auto track = enrich(library, file);
    auto state = library.snapshot(); state["tracks"][0].erase("metadataParserVersion");
    write(library.directory() / L"library.json", state.dump());
    dr::Library upgraded(library.directory()); const auto loaded = upgraded.load();
    CHECK(loaded["tracks"][0]["metadataPending"] == true); CHECK(loaded["tracks"][0]["title"] == track["title"]);
    CHECK(upgraded.pending_metadata().size() == 1);
    const auto refreshed = upgraded.enrich_metadata(upgraded.pending_metadata()[0]); CHECK(refreshed["metadataParserVersion"] == dr::metadata_parser_version);
    upgraded.flush_metadata(); dr::Library restarted(library.directory()); restarted.load(); CHECK(restarted.pending_metadata().empty());
}
}
int main() {
    static_assert(TAGLIB_MAJOR_VERSION == 2 && TAGLIB_MINOR_VERSION == 3 && TAGLIB_PATCH_VERSION == 2);
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); if (FAILED(initialized)) return 1;
    const auto root = dr::fs::temp_directory_path() / ("dr-music-metadata-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(dr::timestamp()));
    dr::fs::create_directories(root); int code = 0;
    try {
        image_test(); embedded_test(root); external_test(root); compressed_test(root); flac_test(root); migration_test(root);
        std::cout << "PASS: TagLib 2.3.2, compressed ID3, cover selection/content detection/resize/fallback, cache recovery, versioned resources and metadata migration\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; code = 1; }
    dr::fs::remove_all(root); CoUninitialize(); return code;
}
