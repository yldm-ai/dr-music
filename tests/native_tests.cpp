#include "library.h"
#include "media.h"
#include "window.h"
#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>
#include <fstream>
#include <iostream>
#include <cstring>
#include <windowsx.h>
#define CHECK(value) do { if (!(value)) throw std::runtime_error("Check failed at line " + std::to_string(__LINE__) + ": " #value); } while (false)
namespace {
void write(const dr::fs::path &file, const std::string &text) { std::ofstream out(file, std::ios::binary); out.write(text.data(), text.size()); }
void wave(const dr::fs::path &file) {
    const unsigned rate = 22050, count = rate * 16 * 2; std::string bytes(44 + count, '\0');
    auto word = [&](size_t pos, unsigned value, unsigned width) { for (unsigned i = 0; i < width; ++i) bytes[pos + i] = static_cast<char>((value >> (i * 8)) & 255); };
    bytes.replace(0, 4, "RIFF"); word(4, count + 36, 4); bytes.replace(8, 8, "WAVEfmt "); word(16, 16, 4);
    word(20, 1, 2); word(22, 1, 2); word(24, rate, 4); word(28, rate * 2, 4); word(32, 2, 2); word(34, 16, 2); bytes.replace(36, 4, "data"); word(40, count, 4); write(file, bytes);
}
template<class F> bool throws(F f) { try { f(); return false; } catch (const std::exception &) { return true; } }
// Range parsing and bounded streams must not leak bytes outside the response.
void media_test(const dr::fs::path &root) {
    CHECK(dr::byte_range("bytes=10-19", 100)->start == 10); CHECK(dr::byte_range("bytes=90-", 100)->end == 99);
    CHECK(dr::byte_range("bytes=-15", 100)->start == 85); CHECK(!dr::byte_range("", 0));
    for (auto value : {"bytes=100-", "bytes=20-10", "bytes=1-2,4-5", "bytes=-0", "bytes=-", "junk", "bytes=18446744073709551616-"}) CHECK(!dr::byte_range(value, 100));
    auto file = root / L"bytes.bin"; write(file, "0123456789abcdefghij"); auto stream = dr::file_slice(file, 5, 6);
    char buffer[32]{}; ULONG count = 0; CHECK(stream->Read(buffer, sizeof(buffer), &count) == S_FALSE); CHECK(count == 6); CHECK(std::string(buffer, count) == "56789a");
    LARGE_INTEGER delta{}; delta.QuadPart = -2; CHECK(SUCCEEDED(stream->Seek(delta, STREAM_SEEK_END, nullptr)));
    Microsoft::WRL::ComPtr<IStream> clone; CHECK(SUCCEEDED(stream->Clone(&clone))); CHECK(SUCCEEDED(clone->Read(buffer, 2, &count))); CHECK(std::string(buffer, count) == "9a");
    STATSTG stat{}; CHECK(SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME))); CHECK(stat.cbSize.QuadPart == 6);
    delta.QuadPart = -7; CHECK(FAILED(stream->Seek(delta, STREAM_SEEK_SET, nullptr)));
}
// Generated tagged WAVs verify native metadata, deduplication and saved playlists.
void library_test(const dr::fs::path &root) {
    auto songs = root / L"音乐" / L"nested"; dr::fs::create_directories(songs);
    auto audio = songs / L"测试歌曲.wav"; wave(audio);
    { TagLib::FileRef ref(audio.c_str()); CHECK(!ref.isNull()); ref.tag()->setTitle(TagLib::String("测试歌曲", TagLib::String::UTF8)); ref.tag()->setArtist(TagLib::String("测试艺术家", TagLib::String::UTF8)); ref.tag()->setAlbum("Local Album"); CHECK(ref.save()); }
    auto lrc = songs / L"测试歌曲.lrc"; write(lrc, "[00:01.00]本地歌词");
    dr::Library library(root / L"data"); library.load(); auto result = library.import_paths({songs.parent_path(), audio});
    CHECK(result["imported"] == 1); auto tracks = result["library"]["tracks"]; CHECK(tracks.size() == 1);
    auto t = tracks[0]; const auto id = t["id"].get<std::string>(); CHECK(t["artist"] == "测试艺术家"); CHECK(t["duration"].get<double>() >= 15.9); CHECK(id.size() == 24);
    CHECK(library.lyrics(id)["text"] == "[00:01.00]本地歌词");
    library.update({{"favorites", {id, id, "unknown"}}, {"playlists", {{{"id", "mix"}, {"name", "Mix"}, {"trackIds", {id}}}}}, {"settings", {{"volume", 20}, {"theme", "light"}, {"titleBar", "system"}}}});
    dr::Library fresh(root / L"data"); fresh.load(); auto saved = fresh.snapshot(); CHECK(saved["favorites"].size() == 1); CHECK(saved["settings"]["volume"] == 1); CHECK(saved["settings"]["titleBar"] == "system");
    CHECK(throws([&] { fresh.update({{"playlists", {{{"id", "x"}, {"name", ""}, {"trackIds", {id}}}}}}); }));
    fresh.import_paths({audio}); CHECK(fresh.snapshot()["tracks"].size() == 1);
    auto resource = dr::serve_media(fresh, L"https://media.dr-music.local/audio/" + dr::wide(id), "bytes=44-99", "GET");
    CHECK(resource.status == 206); STATSTG stat{}; CHECK(SUCCEEDED(resource.stream->Stat(&stat, STATFLAG_NONAME))); CHECK(stat.cbSize.QuadPart == 56);
    CHECK(dr::serve_media(fresh, L"https://media.dr-music.local/audio/../../secret", "", "GET").status == 404);
    CHECK(dr::serve_media(fresh, L"https://media.dr-music.local/audio/" + dr::wide(id), "bytes=99999999-", "GET").status == 416);
    auto utf16 = songs / L"utf16.lrc"; std::string utf16_bytes("\xff\xfe");
    for (wchar_t c : std::wstring(L"[00:01.00]A")) { utf16_bytes += static_cast<char>(c & 255); utf16_bytes += static_cast<char>(c >> 8); }
    write(utf16, utf16_bytes);
    CHECK(fresh.attach_lyrics(id, utf16)["text"] == "[00:01.00]A");
    CHECK(throws([&] { fresh.lyrics("unknown"); }));
    fresh.remove(id); CHECK(fresh.snapshot()["playlists"][0]["trackIds"].empty()); CHECK(dr::fs::exists(audio));
}
// Recovery must preserve the original index bytes, including invalid UTF-8.
void recovery_test(const dr::fs::path &root) {
    auto data = root / L"corrupt"; dr::fs::create_directory(data); write(data / L"library.json", "{broken"); dr::Library library(data);
    CHECK(library.load().contains("notice")); bool backup = false;
    for (const auto &p : dr::fs::directory_iterator(data)) if (p.path().filename().wstring().starts_with(L"library.corrupt-")) { CHECK(dr::read_text(p.path()) == "{broken"); backup = true; }
    CHECK(backup); library.update({{"settings", {{"volume", .4}}}}); CHECK(dr::Json::parse(dr::read_text(data / L"library.json"))["version"] == 1);
}
// Native hit testing, maximization and caption mode switching stay outside WebView2.
void caption_test() {
    dr::Window window; CHECK(window.create(GetModuleHandleW(nullptr), true, true)); auto hwnd = window.handle();
    auto bounds = window.content_bounds(); CHECK(bounds.top > 0);
    auto hit = [&](int x, int y) { POINT p{x, y}; ClientToScreen(hwnd, &p); return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y)); };
    const auto width = MulDiv(46, GetDpiForWindow(hwnd), 96); CHECK(hit(80, bounds.top / 2) == HTCAPTION);
    CHECK(hit(bounds.right - width * 3 / 2, bounds.top / 2) == HTMAXBUTTON); CHECK(hit(bounds.right - width / 2, bounds.top / 2) == HTCLIENT);
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0); CHECK(IsZoomed(hwnd)); SendMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0); CHECK(!IsZoomed(hwnd));
    window.set_style(false); CHECK(window.content_bounds().top == 0); window.set_style(true); CHECK(window.content_bounds().top > 0);
    window.set_appearance(false, false, L"DR Music Test"); wchar_t title[100]{}; GetWindowTextW(hwnd, title, 100); CHECK(std::wstring(title) == L"DR Music Test"); DestroyWindow(hwnd);
}
// Re-arming a leave notification outside a window previously created an
// endless posted-message loop that starved WM_PAINT after maximizing.
void caption_leave_test() {
    dr::Window window; CHECK(window.create(GetModuleHandleW(nullptr), true, true)); auto hwnd = window.handle();
    ShowWindow(hwnd, SW_HIDE);
    MSG message{};
    while (PeekMessageW(&message, hwnd, WM_MOUSELEAVE, WM_MOUSELEAVE, PM_REMOVE)) {}
    SendMessageW(hwnd, WM_MOUSELEAVE, 0, 0);
    unsigned notifications = 0;
    while (notifications < 8 && PeekMessageW(&message, hwnd, WM_MOUSELEAVE, WM_MOUSELEAVE, PM_REMOVE)) {
        ++notifications; DispatchMessageW(&message);
    }
    CHECK(notifications < 8);
    DestroyWindow(hwnd);
}
}
int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto root = dr::fs::temp_directory_path() / ("dr-music-native-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(dr::timestamp()));
    dr::fs::create_directories(root); int code = 0;
    try { media_test(root); library_test(root); recovery_test(root); caption_test(); caption_leave_test(); std::cout << "PASS: native metadata/library/persistence/lyrics, bounded media streaming, ranges, D2D caption hit tests, leave-message regression and system caption switching\n"; }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; code = 1; }
    dr::fs::remove_all(root); CoUninitialize(); return code;
}
