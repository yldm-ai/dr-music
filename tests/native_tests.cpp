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
#include <dwmapi.h>
#include <chrono>
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
    CHECK(tracks[0]["duration"] == 0); CHECK(tracks[0]["artist"] == "未知艺术家"); CHECK(tracks[0]["metadataPending"] == true);
    for (const auto &candidate : result["pending"]) CHECK(!library.enrich_metadata(candidate).is_null());
    library.flush_metadata(); tracks = library.snapshot()["tracks"];
    auto t = tracks[0]; const auto id = t["id"].get<std::string>(); CHECK(t["artist"] == "测试艺术家"); CHECK(t["duration"].get<double>() >= 15.9); CHECK(id.size() == 24);
    CHECK(library.lyrics(id)["text"] == "[00:01.00]本地歌词");
    library.update({{"favorites", {id, id, "unknown"}}, {"playlists", {{{"id", "mix"}, {"name", "Mix"}, {"trackIds", {id}}}}}, {"settings", {{"volume", 20}, {"theme", "light"}, {"titleBar", "system"}}}});
    dr::Library fresh(root / L"data"); fresh.load(); auto saved = fresh.snapshot(); CHECK(saved["favorites"].size() == 1); CHECK(saved["settings"]["volume"] == 1); CHECK(saved["settings"]["titleBar"] == "system");
    fresh.update({{"settings", {{"titleBar", "webview2"}}}});
    dr::Library web_settings(root / L"data"); web_settings.load(); CHECK(web_settings.snapshot()["settings"]["titleBar"] == "webview2");
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
    auto other = root / L"其他音乐"; dr::fs::create_directories(other);
    auto outside = other / L"保留歌曲.wav"; wave(outside);
    auto restored = fresh.import_paths({songs.parent_path(), other, outside});
    auto folder = dr::path_text(songs.parent_path());
    CHECK(restored["library"]["folders"].size() == 2);
    CHECK(restored["library"]["tracks"].size() == 2);
    std::string inside_id, outside_id;
    for (const auto &track : restored["library"]["tracks"]) {
        const auto path = track["path"].get<std::string>();
        if (dr::path_key(dr::from_text(path)) == dr::path_key(audio)) inside_id = track["id"].get<std::string>();
        else outside_id = track["id"].get<std::string>();
    }
    CHECK(!inside_id.empty()); CHECK(!outside_id.empty());
    fresh.update({{"favorites", {inside_id, outside_id}}, {"playlists", {{{"id", "mix"}, {"name", "Mix"}, {"trackIds", {inside_id, outside_id}}}}}});
    CHECK(throws([&] { fresh.remove_folder(dr::path_text(other / L"不存在")); }));
    auto after_removal = fresh.remove_folder(folder);
    CHECK(after_removal["folders"].size() == 1);
    CHECK(after_removal["tracks"].size() == 1);
    CHECK(dr::path_key(dr::from_text(after_removal["tracks"][0]["path"].get<std::string>())) == dr::path_key(outside));
    CHECK(after_removal["favorites"].size() == 1); CHECK(after_removal["favorites"][0] == outside_id);
    CHECK(after_removal["playlists"][0]["trackIds"].size() == 1); CHECK(after_removal["playlists"][0]["trackIds"][0] == outside_id);
    CHECK(dr::fs::exists(audio)); CHECK(dr::fs::exists(outside));
    dr::Library reloaded(root / L"data"); reloaded.load(); CHECK(reloaded.snapshot()["folders"] == after_removal["folders"]);
}
// Metadata is optional for admission and old background jobs cannot restore
// removed songs or overwrite the result of a newer rescan.
void deferred_metadata_test(const dr::fs::path &root) {
    auto songs = root / L"deferred"; dr::fs::create_directory(songs);
    auto audio = songs / L"filename.wav"; wave(audio);
    { TagLib::FileRef ref(audio.c_str()); ref.tag()->setTitle("Tagged title"); CHECK(ref.save()); }
    dr::Library library(root / L"deferred-data"); library.load();
    auto first = library.import_paths({songs});
    CHECK(first["library"]["tracks"][0]["title"] == "filename");
    CHECK(first["library"]["tracks"][0]["metadataPending"] == true);
    dr::Library resumed(root / L"deferred-data"); resumed.load();
    CHECK(resumed.pending_metadata().size() == 1);
    auto second = library.import_paths({songs});
    CHECK(library.enrich_metadata(first["pending"][0]).is_null());
    auto enriched = library.enrich_metadata(second["pending"][0]);
    CHECK(enriched["title"] == "Tagged title"); CHECK(enriched["duration"].get<double>() >= 15.9);
    CHECK(enriched["metadataPending"] == false); library.flush_metadata();
    auto third = library.import_paths({songs});
    CHECK(third["library"]["tracks"][0]["title"] == "Tagged title");
    library.remove_folder(dr::path_text(songs));
    CHECK(library.enrich_metadata(third["pending"][0]).is_null());
    CHECK(library.snapshot()["tracks"].empty()); CHECK(dr::fs::exists(audio));
}
// A large cached library, missing directories, nested roots, similarly named
// siblings and individually imported songs require no disk traversal to remove.
void bulk_remove_test(const dr::fs::path &root) {
    auto data = root / L"bulk-data"; dr::fs::create_directory(data);
    auto songs = root / L"offline-songs";
    auto state = dr::Library::defaults();
    state["folders"] = {dr::path_text(songs), dr::path_text(songs / L"nested"), dr::path_text(root / L"offline-songs-other")};
    dr::Json ids = dr::Json::array();
    for (int i = 0; i < 10000; ++i) {
        const auto id = std::to_string(i); ids.push_back(id);
        state["tracks"].push_back({{"id", id}, {"path", dr::path_text(songs / L"nested" / (id + ".wav"))}});
    }
    for (const auto &[id, file] : std::vector<std::pair<std::string, dr::fs::path>>{
            {"sibling", root / L"offline-songs-other" / L"keep.wav"}, {"single", root / L"single.wav"}}) {
        state["tracks"].push_back({{"id", id}, {"path", dr::path_text(file)}}); ids.push_back(id);
    }
    state["favorites"] = ids;
    for (int i = 0; i < 6; ++i) state["playlists"].push_back({{"id", std::to_string(i)}, {"name", "Bulk"}, {"trackIds", ids}});
    state["tracks"][0]["cover"] = "image/png"; write(data / L"0.cover", "cached cover");
    write(data / L"library.json", state.dump());
    dr::Library library(data); library.load();
    // A failed atomic save must leave the current library and cover intact.
    dr::fs::create_directory(data / L"library.json.tmp");
    CHECK(throws([&] { library.remove_folder(dr::path_text(songs)); }));
    CHECK(library.snapshot() == state); CHECK(dr::fs::exists(data / L"0.cover"));
    dr::fs::remove(data / L"library.json.tmp");
    const auto started = std::chrono::steady_clock::now();
    const auto removed = library.remove_folder(dr::path_text(songs));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    std::cout << "Bulk removal: 10000 songs, 6 playlists in " << elapsed << " ms\n";
    CHECK(removed["tracks"].size() == 2); CHECK(removed["folders"].size() == 1);
    CHECK(removed["favorites"] == dr::Json({"sibling", "single"}));
    for (const auto &p : removed["playlists"]) CHECK(p["trackIds"] == removed["favorites"]);
    CHECK(!dr::fs::exists(data / L"0.cover"));
    dr::Library reloaded(data); CHECK(reloaded.load() == removed);
}
// Recovery must preserve the original index bytes, including invalid UTF-8.
void recovery_test(const dr::fs::path &root) {
    auto data = root / L"corrupt"; dr::fs::create_directory(data); write(data / L"library.json", "{broken"); dr::Library library(data);
    CHECK(library.load().contains("notice")); bool backup = false;
    for (const auto &p : dr::fs::directory_iterator(data)) if (p.path().filename().wstring().starts_with(L"library.corrupt-")) { CHECK(dr::read_text(p.path()) == "{broken"); backup = true; }
    CHECK(backup); library.update({{"settings", {{"volume", .4}}}}); CHECK(dr::Json::parse(dr::read_text(data / L"library.json"))["version"] == 1);
}
// Native hit testing, maximization and caption mode switching stay outside WebView2.
void check_cloaked(HWND hwnd) {
    DWORD cloaked = 0;
    CHECK(SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))));
    CHECK(cloaked & DWM_CLOAKED_APP);
}
void caption_test() {
    dr::Window window; CHECK(window.create(GetModuleHandleW(nullptr), true, true, true)); auto hwnd = window.handle();
    check_cloaked(hwnd);
    auto bounds = window.content_bounds(); CHECK(bounds.top > 0);
    auto hit = [&](int x, int y) { POINT p{x, y}; ClientToScreen(hwnd, &p); return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y)); };
    const auto width = MulDiv(46, GetDpiForWindow(hwnd), 96); CHECK(hit(80, bounds.top / 2) == HTCAPTION);
    CHECK(hit(bounds.right - width * 3 / 2, bounds.top / 2) == HTMAXBUTTON); CHECK(hit(bounds.right - width / 2, bounds.top / 2) == HTCLIENT);
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0); CHECK(IsZoomed(hwnd)); check_cloaked(hwnd); SendMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0); CHECK(!IsZoomed(hwnd));
    window.set_style(false); CHECK(window.content_bounds().top == 0);
    window.set_style(false, true); CHECK(window.content_bounds().top == 0);
    window.set_style(true); CHECK(window.content_bounds().top > 0);
    std::wstring tip; RECT anchor{};
    window.on_caption_tooltip = [&](const std::wstring &text, RECT rect) { tip = text; anchor = rect; };
    SendMessageW(hwnd, WM_NCMOUSEMOVE, HTMAXBUTTON, 0);
    SendMessageW(hwnd, WM_TIMER, 43, 0);
    CHECK(tip == L"最大化"); CHECK(anchor.right - anchor.left == width);
    window.set_appearance(false, true); CHECK(tip.empty());
    window.set_appearance(false, false); check_cloaked(hwnd); wchar_t title[100]{}; GetWindowTextW(hwnd, title, 100); CHECK(std::wstring(title) == L"DR Music"); DestroyWindow(hwnd);
}
// Re-arming a leave notification outside a window previously created an
// endless posted-message loop that starved WM_PAINT after maximizing.
void caption_leave_test() {
    dr::Window window; CHECK(window.create(GetModuleHandleW(nullptr), true, true, true)); auto hwnd = window.handle();
    check_cloaked(hwnd);
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
// The composition WebView2 leaves hit testing to the host. The page reports
// the caption rectangles, and the host turns the maximize button into a real
// non-client HTMAXBUTTON so Windows 11 Snap Layouts works without an overlay.
void webview_caption_test() {
    dr::Window window; CHECK(window.create(GetModuleHandleW(nullptr), false, true, true));
    window.set_style(false, true); auto hwnd = window.handle();
    check_cloaked(hwnd);
    CHECK(window.content_bounds().top == 0);
    CHECK(FindWindowExW(hwnd, nullptr, L"DRMusic.CaptionHitTest", nullptr) == nullptr);
    RECT client{}; GetClientRect(hwnd, &client);
    const int dpi = GetDpiForWindow(hwnd), width = MulDiv(46, dpi, 96), height = MulDiv(36, dpi, 96);
    RECT close{client.right - width, 0, client.right, height};
    RECT maximize{client.right - width * 2, 0, client.right - width, height};
    RECT minimize{client.right - width * 3, 0, client.right - width * 2, height};
    window.set_caption_layout({0, 0, client.right, height}, minimize, maximize, close);
    auto hit = [&](int x, int y) { POINT p{x, y}; ClientToScreen(hwnd, &p); return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y)); };
    CHECK(hit(minimize.left + width / 2, height / 2) == HTCLIENT);
    CHECK(hit(maximize.left + width / 2, height / 2) == HTMAXBUTTON);
    CHECK(hit(close.left + width / 2, height / 2) == HTCLIENT);
    CHECK(hit(80, height / 2) == HTCAPTION);
    CHECK(hit(80, height + 20) == HTCLIENT);
    // WebView2 can round its viewport one device pixel past the client rect.
    window.set_caption_layout({0, 0, client.right, height}, minimize, maximize, {close.left, 0, client.right + 1, height});
    CHECK(hit(close.left + width / 2, height / 2) == HTCLIENT);
    // A resize invalidates the old page rectangles until the next report.
    SendMessageW(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
    CHECK(hit(maximize.left + width / 2, height / 2) == HTMAXBUTTON);
    std::vector<std::wstring> events;
    window.on_caption_event = [&](const std::wstring &event) { events.push_back(event); };
    std::wstring tip; RECT anchor{};
    window.on_caption_tooltip = [&](const std::wstring &text, RECT button) { tip = text; anchor = button; };
    POINT center{maximize.left + width / 2, height / 2}; ClientToScreen(hwnd, &center);
    SendMessageW(hwnd, WM_NCMOUSEMOVE, HTMAXBUTTON, MAKELPARAM(center.x, center.y));
    CHECK(!events.empty() && events.back() == L"enter");
    SendMessageW(hwnd, WM_TIMER, 43, 0);
    CHECK(tip == L"最大化"); CHECK(anchor.right - anchor.left == width);
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTMAXBUTTON, MAKELPARAM(center.x, center.y));
    CHECK(events.back() == L"down");
    SendMessageW(hwnd, WM_NCLBUTTONUP, HTMAXBUTTON, MAKELPARAM(center.x, center.y));
    CHECK(events.back() == L"up");
    MSG message{};
    while (PeekMessageW(&message, hwnd, WM_SYSCOMMAND, WM_SYSCOMMAND, PM_REMOVE)) DispatchMessageW(&message);
    CHECK(IsZoomed(hwnd));
    check_cloaked(hwnd);
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0); CHECK(!IsZoomed(hwnd));
    SendMessageW(hwnd, WM_NCMOUSELEAVE, 0, 0); CHECK(events.back() == L"leave");
    DestroyWindow(hwnd);
}
}
int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto root = dr::fs::temp_directory_path() / ("dr-music-native-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(dr::timestamp()));
    dr::fs::create_directories(root); int code = 0;
    try { media_test(root); library_test(root); deferred_metadata_test(root); bulk_remove_test(root); recovery_test(root); caption_test(); caption_leave_test(); webview_caption_test(); std::cout << "PASS: native metadata/library/persistence/lyrics, bounded media streaming, ranges, D2D caption hit tests, leave-message regression, WebView2 caption hit testing and switching\n"; }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; code = 1; }
    dr::fs::remove_all(root); CoUninitialize(); return code;
}
