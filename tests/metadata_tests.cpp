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
    dr::CoverCache cache;
    const auto bytes = bitmap(16, 16, 0x123456);
    const auto first = cache.decode(bytes), second = cache.decode(bytes);
    CHECK(first && first == second); // Identical artwork shares the decoded result.
    CHECK(cache.decode(bitmap(16, 16, 0x654321)) != first);
}
struct ExclusiveRead {
    HANDLE handle;
    explicit ExclusiveRead(const dr::fs::path &file) : handle(CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) { CHECK(handle != INVALID_HANDLE_VALUE); }
    ~ExclusiveRead() { CloseHandle(handle); }
};
void bounded_cache_test() {
    dr::BoundedCache<std::string> cache(6, 2);
    cache.put("a", std::make_shared<std::string>("abc"), 3);
    cache.put("b", std::make_shared<std::string>("def"), 3);
    const auto pinned = cache.get("a"); CHECK(pinned);
    auto stream = dr::memory_slice(pinned, 1, 2);
    cache.put("c", std::make_shared<std::string>("ghi"), 3);
    CHECK(!cache.get("b")); CHECK(cache.get("a")); CHECK(cache.bytes() == 6);
    cache.clear(); CHECK(cache.bytes() == 0); CHECK(*pinned == "abc");
    char bytes[8]{}; ULONG count = 0;
    CHECK(stream->Read(bytes, sizeof(bytes), &count) == S_FALSE); CHECK(std::string(bytes, count) == "bc");
    LARGE_INTEGER offset{}; offset.QuadPart = -1; CHECK(SUCCEEDED(stream->Seek(offset, STREAM_SEEK_END, nullptr)));
    Microsoft::WRL::ComPtr<IStream> clone; CHECK(SUCCEEDED(stream->Clone(&clone)));
    CHECK(SUCCEEDED(clone->Read(bytes, 1, &count))); CHECK(std::string(bytes, count) == "c");
    CHECK(SUCCEEDED(stream->Read(bytes, 1, &count))); CHECK(std::string(bytes, count) == "c");
    offset.QuadPart = -3; CHECK(FAILED(stream->Seek(offset, STREAM_SEEK_SET, nullptr)));
    STATSTG stat{}; CHECK(SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME))); CHECK(stat.cbSize.QuadPart == 2);
    cache.put("oversized", std::make_shared<std::string>("1234567"), 7); CHECK(!cache.get("oversized"));
    CHECK(cache.bytes() == 0);
}
void cache_test(const dr::fs::path &root) {
    const auto songs = root / L"cached-songs"; dr::fs::create_directory(songs);
    const auto file = songs / L"song.wav", cover_file = songs / L"folder.jpg", lyrics = songs / L"song.lrc";
    write(file, wave()); write(cover_file, bitmap(2, 2, 0x102030)); write(lyrics, "[00:01.00]cached");
    dr::Library library(root / L"cache-data"); library.load();
    const auto imported = library.import_paths({songs});
    const auto first = library.enrich_metadata(imported["pending"][0]); library.flush_metadata();
    CHECK(first.contains("metadataFingerprint")); CHECK(first.contains("coverHash"));
    const auto id = first["id"].get<std::string>();
    const auto cached_file = library.directory() / (id + ".cover");
    const auto original_stamp = dr::file_stamp(cached_file);
    const auto original_resource = library.cover_resource(id);
    CHECK(original_resource && original_resource == library.cover_resource(id));
    // Cache hits succeed even while another handle denies opening the file.
    const auto pending = library.import_paths({file});
    {
        ExclusiveRead audio_lock(file), image_lock(cached_file);
        const auto reused = library.enrich_metadata(pending["pending"][0]);
        CHECK(!reused.contains("metadataError")); CHECK(reused["title"] == first["title"]);
        CHECK(reused["coverRevision"] == first["coverRevision"]); CHECK(reused["metadataPending"] == false);
        CHECK(library.cover_resource(id) == original_resource);
        CHECK(dr::file_stamp(cached_file) == original_stamp);
    }
    library.flush_metadata();
    dr::Library restarted(library.directory()); restarted.load();
    const auto restored = restarted.import_paths({file});
    {
        ExclusiveRead audio_lock(file);
        const auto reused = restarted.enrich_metadata(restored["pending"][0]);
        CHECK(!reused.contains("metadataError")); CHECK(reused["metadataFingerprint"] == first["metadataFingerprint"]);
        CHECK(reused["coverRevision"] == first["coverRevision"]);
    }
    CHECK(library.lyrics(id)["text"] == "[00:01.00]cached");
    {
        ExclusiveRead lyric_lock(lyrics);
        CHECK(library.lyrics(id)["text"] == "[00:01.00]cached");
    }
    write(lyrics, "[00:02.00]changed lyrics"); CHECK(library.lyrics(id)["text"] == "[00:02.00]changed lyrics");
    const auto ttml = songs / L"song.ttml"; write(ttml, "<tt/>"); CHECK(library.lyrics(id)["format"] == "ttml");
    const auto attached = songs / L"attached.lrc"; write(attached, "[00:03.00]attached");
    CHECK(library.attach_lyrics(id, attached)["text"] == "[00:03.00]attached");
    {
        TagLib::FileRef ref(file.c_str()); ref.tag()->setTitle("Updated tag"); CHECK(ref.save());
    }
    const auto retagged = enrich(library, file);
    CHECK(retagged["title"] == "Updated tag"); CHECK(retagged["metadataFingerprint"] != first["metadataFingerprint"]);
    CHECK(retagged["coverHash"] == first["coverHash"]); CHECK(retagged["coverRevision"] == first["coverRevision"]);
    CHECK(dr::file_stamp(cached_file) == original_stamp);
    // A higher-priority candidate existed but was corrupt, then is repaired.
    const auto priority_cover = songs / L"song.jpg"; write(priority_cover, "broken");
    const auto fallback = enrich(library, file); CHECK(fallback["coverHash"] == first["coverHash"]);
    write(priority_cover, bitmap(2, 2, 0x405060));
    const auto changed = enrich(library, file); CHECK(changed["coverSource"] == "song.jpg");
    CHECK(changed["coverHash"] != first["coverHash"]); CHECK(changed["coverRevision"] != first["coverRevision"]);
    CHECK(pixel(cover_bytes(library, changed)) == 0x405060);
    const auto uri = L"https://media.dr-music.local/cover/" + dr::wide(id);
    const auto hash_uri = uri + L"?v=" + dr::wide(changed["coverHash"].get<std::string>());
    auto response = dr::serve_media(library, hash_uri, "", "GET");
    CHECK(response.status == 200); CHECK(response.headers.find(L"immutable") != std::wstring::npos);
    CHECK(dr::serve_media(library, uri, "", "GET").headers.find(L"immutable") == std::wstring::npos);
    CHECK(dr::serve_media(library, uri + L"?v=" + dr::wide(first["coverHash"].get<std::string>()), "", "GET").status == 404);
    const auto range = dr::serve_media(library, hash_uri, "bytes=1-3", "GET");
    char bytes[8]{}; ULONG count = 0;
    CHECK(range.status == 206); CHECK(range.stream->Read(bytes, 8, &count) == S_FALSE); CHECK(count == 3);
    CHECK(std::string(bytes, count) == original_resource->bytes.substr(1, 3));
    const auto head = dr::serve_media(library, hash_uri, "", "HEAD"); CHECK(head.status == 200); CHECK(!head.stream);
    // A missing disk cache is regenerated even with unchanged source files.
    dr::fs::remove(cached_file);
    const auto repaired = enrich(library, file); CHECK(dr::fs::exists(cached_file)); CHECK(repaired["coverHash"] == changed["coverHash"]);
    dr::fs::remove(priority_cover); dr::fs::remove(cover_file);
    const auto cleared = enrich(library, file); CHECK(!cleared.contains("cover")); CHECK(!library.cover_resource(id));
    write(cover_file, bitmap(2, 2, 0x708090)); const auto added = enrich(library, file); CHECK(added.contains("cover"));
    library.remove_folder(dr::path_text(songs)); CHECK(!library.cover_resource(id));
    CHECK(dr::serve_media(library, hash_uri, "", "GET").status == 404); CHECK(!dr::fs::exists(cached_file));
    // In-flight streams own their immutable bytes even after cache invalidation.
    CHECK(SUCCEEDED(response.stream->Read(bytes, sizeof(bytes), &count))); CHECK(count == 8);
    CHECK(dr::fs::exists(file)); CHECK(dr::fs::exists(lyrics));
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
        bounded_cache_test(); image_test(); embedded_test(root); external_test(root); compressed_test(root); flac_test(root); migration_test(root); cache_test(root);
        std::cout << "PASS: TagLib 2.3.2, compressed ID3, cover selection/content detection/resize/fallback, cache recovery, versioned resources and metadata migration\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; code = 1; }
    dr::fs::remove_all(root); CoUninitialize(); return code;
}
