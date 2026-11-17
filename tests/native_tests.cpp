#include <winsock2.h> // before windows.h (via library.h): a loopback server that never answers
#include <ws2tcpip.h>
#include "library.h"
#include "media.h"
#include "window.h"
#include "cover.h"
#include "fonts.h"
#include "chinese.h"
#include "online_lyrics.h"
#include "lyric_crypto.h"
#include "http.h"
#include "index_translate.h"
#include "cue.h"
#include "niutrans.h"
#include <zlib.h>
#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>
#include <algorithm>
#include <fstream>
#include <set>
#include <map>
#include <iostream>
#include <sstream>
#include <cstring>
#include <windowsx.h>
#include <dwmapi.h>
#include <chrono>
#include <future>
#include <thread>
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
// Rescan follows the disk: moved / deleted files leave the library (and its references),
// while a plain import never prunes and an unreachable root keeps its tracks.
void rescan_test(const dr::fs::path &root) {
    auto music = root / L"rescan" / L"music"; dr::fs::create_directories(music / L"old");
    wave(music / L"old" / L"a.wav"); wave(music / L"old" / L"b.wav"); wave(music / L"keep.wav");
    dr::Library library(root / L"rescan-data"); library.load(); library.import_paths({music});
    auto ids = library.snapshot()["tracks"]; CHECK(ids.size() == 3);
    std::string a_id; for (const auto &t : ids) if (t["path"].get<std::string>().ends_with("a.wav")) a_id = t["id"];
    library.update({{"favorites", {a_id}}, {"playlists", {{{"id", "p"}, {"name", "P"}, {"trackIds", {a_id}}}}}});
    dr::fs::create_directories(music / L"new"); dr::fs::rename(music / L"old" / L"a.wav", music / L"new" / L"a.wav"); dr::fs::remove(music / L"old" / L"b.wav");
    library.import_paths({music / L"new"}); CHECK(library.snapshot()["tracks"].size() == 4); // plain import adds, never prunes
    auto result = library.import_paths({music}, {}, {}, true);
    auto state = library.snapshot(); CHECK(result["removed"] == 2); CHECK(state["tracks"].size() == 2);
    for (const auto &t : state["tracks"]) CHECK(t["path"].get<std::string>().find("\\old\\") == std::string::npos);
    CHECK(state["favorites"].empty()); CHECK(state["playlists"][0]["trackIds"].empty());
    dr::fs::remove_all(root / L"rescan" / L"music"); // the whole root is unreachable now
    auto offline = library.import_paths({music}, {}, {}, true);
    CHECK(offline["removed"] == 0); CHECK(library.snapshot()["tracks"].size() == 2);
}
// Provider codecs: CNG MD5 / AES against published vectors, KRC framing, and QQ's DES variant
// against a vector produced by AMLL's own encryptQrcHex (@applemusic-like-lyrics/lyric).
void lyric_crypto_test() {
    CHECK(dr::md5_hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    // FIPS-197 C.1 single block (followed by one PKCS#7 padding block).
    const std::string key("\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f", 16), block("\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa\xbb\xcc\xdd\xee\xff", 16);
    CHECK(dr::hex_upper(dr::aes128_ecb_encrypt(block, key)).starts_with("69C4E0D86A7B0430D8CDB78070B4C55A"));
    CHECK(dr::base64_decode(dr::base64_encode("歌词 lyrics")) == "歌词 lyrics");
    const std::string qrc = "[0,1500]测(0,500)试(500,500)歌(1000,500)\n[1500,1500]Second(1500,700) line(2200,800)";
    CHECK(dr::decrypt_qrc("aae13cd5f0b12c79383b3dcfa8782dc30fdf64d4906eee3f361f6fbd2b17d6e4c763358bee725754fa5b5c07c9a242f9f8e3a19b944784397e0f2b8d226e00a0f44296543d3cf20d1f10d784c9257240") == qrc);
    CHECK(dr::decrypt_qrc(dr::encrypt_qrc(qrc)) == qrc);
    CHECK(throws([] { dr::decrypt_qrc("zz"); }));
    CHECK(dr::url_encode("a b/歌") == "a%20b%2F%E6%AD%8C");
    CHECK(dr::parse_url("https://Lrclib.net/api/search?x=1")->host == "lrclib.net"); CHECK(dr::parse_url("http://a.b:8080/p")->port == 8080);
    CHECK(!dr::parse_url("https://user@evil.com/")); CHECK(!dr::parse_url("file:///c:/x"));
}
// Online lyrics end to end against fixtures served by fixture_fetch (no network): AMLL matching
// (script folding, aliases, untagged file names, newest revision), every provider's request /
// response handling and decoders, ordering, caching and the host allowlist.
void online_lyrics_test(const dr::fs::path &root) {
    auto fixture = root / L"fixture";
    auto put = [&](const std::string &host_and_path, const std::string &body) {
        auto file = fixture / dr::from_text(host_and_path); dr::fs::create_directories(file.parent_path()); write(file, body); };
    const std::string amll = "raw.githubusercontent.com/amll-dev/amll-ttml-db/main/";
    auto entry = [](const std::string &title, const std::string &artist, const std::string &album, const std::string &file) {
        dr::Json metadata = dr::Json::array();
        for (const auto &[k, v] : std::vector<std::pair<std::string, std::string>>{{"album", album}, {"artists", artist}, {"musicName", title}, {"ncmMusicId", "1"}})
            metadata.push_back(dr::Json::array({k, dr::Json::array({v})}));
        return dr::Json{{"metadata", metadata}, {"rawLyricFile", file}}.dump() + "\n"; };
    put(amll + "metadata/raw-lyrics-index.jsonl",
        entry("後來", "劉若英", "我等你", "100-1-aaaa.ttml") + entry("後來", "劉若英", "我等你", "200-1-bbbb.ttml") +
        entry("Idol", "YOASOBI", "Idol", "300-1-cccc.ttml") + entry("写给明天", "某歌手", "某专辑", "400-1-dddd.ttml") +
        "not json\n" + entry("Bad", "X", "Y", "../escape.ttml") +
        R"J({"metadata":[["album",["THE BOOK 3"]],["artists",["YOASOBI","Ikura"]],["musicName",["勇者","The Brave"]]],"rawLyricFile":"500-1-eeee.ttml"})J" "\n");
    put(amll + "raw-lyrics/200-1-bbbb.ttml", "<tt xmlns=\"http://www.w3.org/ns/ttml\"><body/></tt>");
    dr::ChineseText chinese(DR_TEST_OPENCC_DIR);
    auto fold = [&](const std::string &text) { return chinese.simplify(text); };
    dr::OnlineLyrics online(root / L"lyrics-cache", dr::fixture_fetch(fixture), fold);
    auto match = online.find({{"title", "后来"}, {"artist", "刘若英"}, {"album", "我等你"}});
    CHECK(match && match->file == "200-1-bbbb.ttml" && match->score == 100); // simplified input, newest revision
    CHECK(online.fetch(match->file).find("<tt") == 0);
    CHECK(dr::fs::exists(root / L"lyrics-cache" / L"files" / L"200-1-bbbb.ttml"));
    CHECK(online.find({{"title", "Idol (Live)"}, {"artist", "YOASOBI / Ayase"}})->file == "300-1-cccc.ttml");
    CHECK(!online.find({{"title", "Idol"}, {"artist", "Someone Else"}}));          // artist must match when known
    CHECK(online.find({{"title", "写给明天"}, {"artist", "未知艺术家"}})->file == "400-1-dddd.ttml");
    CHECK(!online.find({{"title", "写给明天 (Live)"}, {"artist", "未知艺术家"}}));
    CHECK(!online.find({{"title", "Bad"}, {"artist", "X"}}));                      // unsafe file names never enter the index
    CHECK(online.find({{"title", "The Brave"}, {"artist", "Ikura"}})->file == "500-1-eeee.ttml");  // any alias
    CHECK(online.find({{"title", "YOASOBI - Idol"}, {"artist", "未知艺术家"}})->file == "300-1-cccc.ttml"); // untagged file name
    CHECK(!online.find({{"title", "Completely Different"}, {"artist", "未知艺术家"}}));
    CHECK(throws([&] { online.fetch("../escape.ttml"); })); CHECK(throws([&] { online.fetch("100-1-aaaa.ttml"); }));
    CHECK(dr::LyricMatcher::similarity("abc", "abc") == 1.0); CHECK(dr::LyricMatcher::similarity("", "x") == 0.0);
    CHECK(dr::LyricMatcher::similarity("martha", "marhta") > .95); // Jaro-Winkler reference: 0.961
    CHECK(online.matcher().fingerprint("YOASOBI - Idol") == online.matcher().fingerprint("Idol YOASOBI"));
    // Durations: same song within 1 s scores full; > 10 s apart is another version.
    dr::LyricMatcher matcher(fold);
    CHECK(matcher.score({"晴天", "周杰伦", "叶惠美", 269}, {{"晴天"}, {"周杰倫"}, {"葉惠美"}, 269.4}) == 100);
    CHECK(matcher.score({"晴天", "周杰伦", "", 269}, {{"晴天"}, {"周杰伦"}, {}, 300}) == 0);
    CHECK(dr::split_artists("A / B、C feat. D & E") == std::vector<std::string>({"A", "B", "C", "D", "E"}));
    CHECK(dr::search_terms({"Song (feat. X)", "Artist", "Album", 0}) == std::vector<std::string>({"Song (feat. X) Artist Album", "Song Artist", "Song"}));
    // NetEase EAPI body, against a reference computed with Node's crypto from Lyricify's EapiHelper algorithm.
    CHECK(dr::netease_eapi_body("/api/song/lyric/v1", {{"id", "1"}}) == "params=04AE33D34A93FE3EC22DA8FA305D290AB337D0FE5F36D211DE0D338CC6AA89D0E8744000D04B3D7A3D495AEE86A48DC4BA9A6E5CBA7E957EED8B6484EC4A40A958A1A8DBDEAC922FD61D5CFFB632AD85F1A74DF512BBC164ECEFA314B2112E14");

    // Every provider, end to end. One song, each provider answering in its own format.
    const dr::Json track = {{"id", "0123456789abcdef01234567"}, {"title", "晴天"}, {"artist", "周杰伦"}, {"album", "叶惠美"}, {"duration", 269.0}};
    put(amll + "metadata/raw-lyrics-index.jsonl", entry("晴天", "周杰倫", "葉惠美", "600-1-ffff.ttml"));
    put(amll + "raw-lyrics/600-1-ffff.ttml", "<tt xmlns=\"http://www.w3.org/ns/ttml\"><body><div><p begin=\"0\" end=\"1\">AMLL</p></div></body></tt>");
    put("lrclib.net/api/search", R"J([{"id":7,"trackName":"晴天","artistName":"周杰伦","albumName":"叶惠美","duration":269,"instrumental":false,"syncedLyrics":"[00:01.00]LRCLIB","plainLyrics":"LRCLIB"}])J");
    put("music.163.com/api/search/get/web", R"J({"code":200,"result":{"songs":[{"id":186016,"name":"晴天","artists":[{"name":"周杰伦"}],"album":{"name":"叶惠美"},"duration":269000},{"id":1,"name":"晴天 (Live)","artists":[{"name":"别人"}],"album":{"name":"x"},"duration":200000}]}})J");
    put("interface3.music.163.com/eapi/song/lyric/v1", R"J({"code":200,"lrc":{"lyric":"[00:01.00]NetEase LRC"},"yrc":{"lyric":"[1000,1000](1000,1000,0)NetEase"},"romalrc":{"lyric":"[00:01.00]ne tto i zu"},"tlyric":{"lyric":"[00:01.00]网易"}})J");
    put("u.y.qq.com/cgi-bin/musicu.fcg", R"J({"req_1":{"data":{"body":{"song":{"list":[{"id":97773,"mid":"0039MnYb0qxYhV","name":"晴天","title":"晴天","singer":[{"name":"周杰伦"}],"album":{"name":"叶惠美"},"interval":269}]}}}}})J");
    const auto qrc_xml = std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?><QrcInfos><LyricInfo LyricCount=\"1\"><Lyric_1 LyricType=\"1\" LyricContent=\"[1000,1000]QQ(1000,1000)&#10;\"/></LyricInfo></QrcInfos>");
    const auto roma_xml = std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?><QrcInfos><LyricInfo LyricCount=\"1\"><Lyric_1 LyricType=\"1\" LyricContent=\"[ti:x]&#10;[1000,1000]kyu (1000,1000)&#10;\"/></LyricInfo></QrcInfos>");
    // The romanization comes first here: <content> must not match <contentroma> / <contentts>.
    put("c.y.qq.com/qqmusic/fcgi-bin/lyric_download.fcg", "<!--<?xml version=\"1.0\" encoding=\"GB2312\" ?><QrcInfos><lyric><contentroma><![CDATA[" + dr::encrypt_qrc(roma_xml) + "]]></contentroma><contentts>[00:01.00]QQ 译文</contentts><content><![CDATA[" + dr::encrypt_qrc(qrc_xml) + "]]></content></lyric></QrcInfos>-->");
    put("mobilecdn.kugou.com/api/v3/search/song", R"J({"data":{"info":[{"hash":"ABCDEF","songname":"晴天","singername":"周杰伦","album_name":"叶惠美","duration":269}]}})J");
    put("lyrics.kugou.com/search", R"J({"candidates":[{"id":"42","accesskey":"KEY"}]})J");
    {
        const std::string krc = "\xEF\xBB\xBF[1000,1000]<0,1000,0>Kugou";
        uLongf size = compressBound(static_cast<uLong>(krc.size())); std::string packed(size, '\0');
        compress(reinterpret_cast<Bytef *>(packed.data()), &size, reinterpret_cast<const Bytef *>(krc.data()), static_cast<uLong>(krc.size())); packed.resize(size);
        static constexpr unsigned char key[] = {0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47, 0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69};
        for (size_t i = 0; i < packed.size(); ++i) packed[i] = static_cast<char>(static_cast<unsigned char>(packed[i]) ^ key[i % 16]);
        put("lyrics.kugou.com/download", dr::Json{{"content", dr::base64_encode("krc1" + packed)}}.dump());
    }
    // lrc.red: the best hit has no lyric (404), so the next recording's TTML is used; bad ISRCs are skipped.
    put("lrc.red/search.json", R"J({"hits":[{"isrc":"../../x","title":"晴天","artist":"周杰伦","album":"葉惠美","duration":269.7},{"isrc":"TWK970300503","title":"晴天","artist":"周杰伦","album":"葉惠美","duration":269.7},{"isrc":"TWK970300504","title":"晴天","artist":"周杰伦","album":"叶惠美","duration":269.7},{"isrc":"TWK970400804","title":"晴天 (Live)","artist":"周杰伦","duration":299.4}],"next":null})J");
    put("lrc.red/s/TWK970300504.ttml", "<tt xmlns=\"http://www.w3.org/ns/ttml\"><body><div><p begin=\"0\" end=\"1\"><span begin=\"0\" end=\"1\">lrc.red</span></p></div></body></tt>");
    // Apple Music: web-player token in a script referenced by the browse page, then the catalog API.
    auto b64url = [](const std::string &json) { auto t = dr::base64_encode(json); std::replace(t.begin(), t.end(), '+', '-'); std::replace(t.begin(), t.end(), '/', '_'); while (!t.empty() && t.back() == '=') t.pop_back(); return t; };
    const auto jwt = b64url("{\"alg\":\"ES256\",\"kid\":\"WebPlayKid\"}") + "." + b64url("{\"iss\":\"AMPWebPlay\",\"exp\":4102444800}") + ".sig";
    const auto stale = b64url("{\"alg\":\"ES256\"}") + "." + b64url("{\"exp\":1}") + ".sig";
    CHECK(dr::apple_pick_token("x=\"" + stale + "\";y=\"" + jwt + "\"", 1800000000) == jwt);
    put("music.apple.com/us/browse", "<script type=\"module\" src=\"/assets/index-abc123.js\"></script>");
    put("music.apple.com/assets/index-abc123.js", "const t=\"" + jwt + "\";");
    put("amp-api.music.apple.com/v1/me/storefront", R"J({"data":[{"id":"cn","attributes":{"defaultLanguageTag":"zh-Hans-CN"}}]})J");
    put("amp-api.music.apple.com/v1/catalog/cn/search", R"J({"results":{"songs":{"data":[{"id":"1234","attributes":{"name":"晴天","artistName":"周杰倫","albumName":"葉惠美","durationInMillis":269000}}]}}})J");
    put("amp-api.music.apple.com/v1/catalog/cn/songs/1234", R"J({"data":[{"relationships":{"syllable-lyrics":{"data":[{"attributes":{"ttml":"<tt><body><p begin=\"0\" end=\"1\"><span begin=\"0\" end=\"1\">Apple</span></p></body></tt>"}}]}}}]})J");

    dr::OnlineLyrics all(root / L"lyrics-all", dr::fixture_fetch(fixture), fold);
    all.set_options({{"amll", "lrcred", "qq", "netease", "kugou", "lrclib", "apple"}, "", "user-token"});
    auto results = all.search(track);
    std::map<std::string, dr::LyricResult> by;
    for (const auto &r : results) by[r.provider] = r;
    CHECK(results.size() == 7);
    CHECK(by["amll"].format == "ttml" && by["amll"].text.find("AMLL") != std::string::npos);
    CHECK(by["lrclib"].format == "lrc" && by["lrclib"].text == "[00:01.00]LRCLIB");
    CHECK(by["netease"].format == "yrc" && by["netease"].text.find("NetEase") != std::string::npos && by["netease"].title == "晴天");
    CHECK(by["qq"].format == "qrc" && by["qq"].text == "[1000,1000]QQ(1000,1000)\n");
    // The providers' own romanization travels with the lyric (and through the cache below).
    CHECK(by["qq"].roman_format == "qrc" && by["qq"].roman == "[ti:x]\n[1000,1000]kyu (1000,1000)\n");
    CHECK(by["netease"].roman_format == "lrc" && by["netease"].roman == "[00:01.00]ne tto i zu");
    CHECK(by["lrclib"].roman.empty() && by["kugou"].roman.empty());
    CHECK(by["netease"].translation_format == "lrc" && by["netease"].translation == "[00:01.00]网易");
    CHECK(by["qq"].translation_format == "lrc" && by["qq"].translation == "[00:01.00]QQ 译文");
    CHECK(by["lrclib"].translation.empty());
    CHECK(by["kugou"].format == "krc" && by["kugou"].text == "[1000,1000]<0,1000,0>Kugou");
    CHECK(by["apple"].format == "ttml" && by["apple"].text.find("Apple") != std::string::npos);
    CHECK(by["lrcred"].format == "ttml" && by["lrcred"].text.find("lrc.red") != std::string::npos && by["lrcred"].reference == "https://lrc.red/s/TWK970300504.ttml");
    for (size_t i = 1; i < results.size(); ++i) CHECK(results[i - 1].score >= results[i].score);
    CHECK(results.front().provider == "amll" || results.front().score > by["amll"].score); // word-level AMLL wins ties
    // Cached: the next lookup works without any provider answering.
    dr::fs::remove_all(fixture);
    {
        const auto cached = all.search(track);
        CHECK(cached.size() == 7);
        CHECK(std::any_of(cached.begin(), cached.end(), [](const dr::LyricResult &r) { return r.provider == "qq" && r.roman_format == "qrc" && !r.roman.empty() && !r.translation.empty(); }));
    }
    // Provider set changes invalidate the cache; disabled providers are not asked.
    all.set_options({{"lrclib"}, "", ""});
    CHECK(all.search(track).empty());
    // A throttled provider (QQ's 2001) is a failure, not "no lyrics": others still answer and the
    // cache entry is marked partial so it is retried soon.
    {
        auto throttled = root / L"fixture-throttled";
        auto file = [&](const std::string &p, const std::string &body) { auto f = throttled / dr::from_text(p); dr::fs::create_directories(f.parent_path()); write(f, body); };
        file("u.y.qq.com/cgi-bin/musicu.fcg", R"J({"code":0,"req_1":{"code":2001,"data":{"body":{"song":{"list":[]}}}}})J");
        file("lrclib.net/api/search", R"J([{"id":7,"trackName":"晴天","artistName":"周杰伦","albumName":"叶惠美","duration":269,"syncedLyrics":"[00:01.00]x"}])J");
        dr::OnlineLyrics partial(root / L"lyrics-partial", dr::fixture_fetch(throttled), fold);
        partial.set_options({{"qq", "lrclib"}, "", ""});
        const auto found = partial.search(track);
        CHECK(found.size() == 1 && found[0].provider == "lrclib");
        const auto cached = dr::Json::parse(dr::read_text(root / L"lyrics-partial" / L"results" / L"0123456789abcdef01234567.json"));
        CHECK(cached["partial"] == true);
    }
    // Host allowlist: provider APIs, plus a configured AMLL mirror only.
    CHECK(all.allowed_host("u.y.qq.com") && all.allowed_host("lrclib.net") && all.allowed_host("lrc.red") && !all.allowed_host("evil.example"));
    all.set_options({{"amll"}, "https://mirror.example/amll-ttml-db", ""});
    CHECK(all.allowed_host("mirror.example"));
    CHECK(!dr::OnlineLyrics::valid_base("http://mirror.example/x") && !dr::OnlineLyrics::valid_base("https://a.b/x?y=1"));

    // Opt-in live check of the real providers (never part of CTest): DR_MUSIC_NETWORK_TEST=1.
    if (char *flag = nullptr; _dupenv_s(&flag, nullptr, "DR_MUSIC_NETWORK_TEST") == 0 && flag) {
        free(flag);
        dr::OnlineLyrics *self = nullptr;
        dr::OnlineLyrics live(root / L"lyrics-live", dr::winhttp_fetch([&](const std::string &host) { return self->allowed_host(host); }), fold);
        self = &live;
        const dr::TrackQuery song{"晴天", "周杰伦", "叶惠美", 269};
        auto http = dr::winhttp_fetch([&](const std::string &host) { return live.allowed_host(host); });
        auto report = [&](const char *name, auto &&body) {
            try { auto r = body(); std::cout << "live " << name << ": " << (r ? r->title + " - " + r->artists + " [" + r->format + ", " + std::to_string(r->score) + "%, " + std::to_string(r->text.size()) + " bytes]" : std::string("no match")) << "\n"; }
            catch (const std::exception &e) { std::cout << "live " << name << ": error " << e.what() << "\n"; }
        };
        report("qq", [&] { return dr::search_qq(http, live.matcher(), song); });
        report("netease", [&] { return dr::search_netease(http, live.matcher(), song); });
        report("kugou", [&] { return dr::search_kugou(http, live.matcher(), song); });
        report("lrclib", [&] { return dr::search_lrclib(http, live.matcher(), song); });
        report("lrcred", [&] { return dr::search_lrcred(http, live.matcher(), song); });
        report("amll", [&]() -> std::optional<dr::LyricResult> { auto m = live.find({{"title", "Idol"}, {"artist", "YOASOBI"}}); if (!m) return std::nullopt; return dr::LyricResult{"amll", m->title, m->artists, m->album, "ttml", live.fetch(m->file), "", 0, m->score}; });
    }
}
// OpenCC folding: traditional text of any region becomes simplified; non-Han text is untouched.
void chinese_test() {
    dr::ChineseText chinese(DR_TEST_OPENCC_DIR); CHECK(chinese.available());
    CHECK(chinese.simplify("後來") == "后来"); CHECK(chinese.simplify("劉若英 - 寫給明天") == "刘若英 - 写给明天");
    CHECK(chinese.simplify("Hello, World") == "Hello, World"); CHECK(chinese.simplify("后来") == "后来");
    auto forms = chinese.query_forms("軟體"); CHECK(forms.front() == "軟體");
    CHECK(std::find(forms.begin(), forms.end(), "软件") != forms.end()); // Taiwan vocabulary through tw2sp
    CHECK(chinese.query_forms("music").size() == 1);
    // 原 / 简 / 繁 for lyrics: markup and timestamps pass through, standards differ in vocabulary.
    CHECK(chinese.convert_script("[00:01.00]测试歌词", "t", "t") == "[00:01.00]測試歌詞");
    CHECK(chinese.convert_script("<span begin=\"1\">后来</span>", "t", "hk") == "<span begin=\"1\">後來</span>");
    CHECK(chinese.convert_script("软件", "t", "twp") == "軟體"); CHECK(chinese.convert_script("软件", "t", "tw") == "軟件");
    CHECK(chinese.convert_script("劉若英 - 寫給明天", "s", "t") == "刘若英 - 写给明天");
    CHECK(chinese.convert_script("后来", "o", "t") == "后来"); CHECK(chinese.convert_script("Hello", "t", "t") == "Hello");
    CHECK(dr::has_han("abc 中")); CHECK(!dr::has_han("こんにちは abc"));
    dr::ChineseText missing(dr::fs::temp_directory_path() / L"dr-music-no-opencc"); CHECK(!missing.available()); CHECK(missing.simplify("後來") == "後來");
}
// DirectWrite enumeration should list installed families once, with names safe for CSS.
void fonts_test() {
    auto fonts = dr::system_font_families(); CHECK(!fonts.empty());
    std::set<std::string> families;
    for (const auto &font : fonts) { CHECK(dr::valid_font_family(font.family)); CHECK(!font.display.empty()); CHECK(font.family.front() != '@'); CHECK(families.insert(font.family).second); }
    CHECK(families.contains("Segoe UI"));
    for (auto bad : {"", "a\"b", "a;b", "a}b", "a\\b", "url(x)", "a\nb"}) CHECK(!dr::valid_font_family(bad));
    CHECK(dr::valid_font_family("Noto Sans SC")); CHECK(dr::valid_font_family("微软雅黑"));
}
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
    CHECK(!library.track(id).contains("lyricRuby"));
    for (const auto *choice : {"jyutping", "romaji", "kana", "romaji-online", ""}) {
        CHECK(library.set_lyric_ruby(id, choice)["lyricRuby"] == choice);
        dr::Library reopened(root / L"data"); reopened.load();
        CHECK(reopened.track(id)["lyricRuby"] == choice);
    }
    CHECK(throws([&] { library.set_lyric_ruby(id, "invalid"); }));
    CHECK(throws([&] { library.set_lyric_ruby("missing", "jyutping"); }));
    CHECK(library.track(id)["lyricRuby"] == "");
    library.set_lyric_ruby(id, "jyutping");
    auto refreshed = library.import_paths({audio});
    for (const auto &candidate : refreshed["pending"]) library.enrich_metadata(candidate);
    library.flush_metadata();
    CHECK(library.track(id)["lyricRuby"] == "jyutping");

    library.update({{"favorites", {id, id, "unknown"}}, {"playlists", {{{"id", "mix"}, {"name", "Mix"}, {"trackIds", {id}}}}}, {"settings", {{"volume", 20}, {"theme", "light"}, {"titleBar", "system"}}}});
    dr::Library fresh(root / L"data"); fresh.load(); auto saved = fresh.snapshot(); CHECK(saved["favorites"].size() == 1); CHECK(saved["settings"]["volume"] == 1); CHECK(saved["settings"]["titleBar"] == "system");
    fresh.update({{"settings", {{"titleBar", "webview2"}}}});
    dr::Library web_settings(root / L"data"); web_settings.load(); CHECK(web_settings.snapshot()["settings"]["titleBar"] == "webview2");
    // Lyric typography is persisted only when it is a safe font name, a CSS weight step and a bounded scale.
    fresh.update({{"settings", {{"lyricFont", "Microsoft YaHei"}, {"lyricWeight", 700}, {"lyricScale", 1.234}}}});
    fresh.update({{"settings", {{"lyricFont", "x\"; } body { color: red"}, {"lyricWeight", 650}, {"lyricScale", 9}}}});
    dr::Library typography(root / L"data"); typography.load(); auto lyric = typography.snapshot()["settings"];
    CHECK(lyric["lyricFont"] == "Microsoft YaHei"); CHECK(lyric["lyricWeight"] == 700); CHECK(lyric["lyricScale"].get<double>() == 1.6);
    fresh.update({{"settings", {{"lyricFont", ""}, {"lyricWeight", 0}, {"lyricScale", 0.1}}}});
    lyric = fresh.snapshot()["settings"]; CHECK(lyric["lyricFont"] == ""); CHECK(lyric["lyricWeight"] == 0); CHECK(lyric["lyricScale"].get<double>() == 0.7);
    CHECK(fresh.snapshot()["settings"]["chineseSearch"] == false);
    fresh.update({{"settings", {{"chineseSearch", true}}}}); fresh.update({{"settings", {{"chineseSearch", "no"}}}});
    dr::Library chinese_setting(root / L"data"); chinese_setting.load(); CHECK(chinese_setting.snapshot()["settings"]["chineseSearch"] == true);
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
        state["tracks"].push_back({{"id", id}, {"path", dr::path_text(songs / L"nested" / (id + ".wav"))}, {"metadataParserVersion", dr::metadata_parser_version}});
    }
    for (const auto &[id, file] : std::vector<std::pair<std::string, dr::fs::path>>{
            {"sibling", root / L"offline-songs-other" / L"keep.wav"}, {"single", root / L"single.wav"}}) {
        state["tracks"].push_back({{"id", id}, {"path", dr::path_text(file)}, {"metadataParserVersion", dr::metadata_parser_version}}); ids.push_back(id);
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
// Whole-CD rips: a .cue splits its audio file into songs with the sheet's names and ranges.
void cue_test(const dr::fs::path &root) {
    const auto sheet = dr::parse_cue("REM DATE 2004\nREM DISCNUMBER 2\nPERFORMER \"周杰伦\"\nTITLE \"七里香\"\nFILE \"disc.flac\" WAVE\n  TRACK 01 AUDIO\n    TITLE \"我的地盘\"\n    INDEX 01 00:00:00\n  TRACK 02 AUDIO\n    TITLE \"七里香\"\n    PERFORMER \"Jay\"\n    INDEX 00 00:04:00\n    INDEX 01 00:05:00\n  TRACK 03 AUDIO\n    TITLE \"借口\"\n    INDEX 01 01:02:37\n", L"C:\\music");
    CHECK(sheet.title == "七里香" && sheet.performer == "周杰伦" && sheet.year == 2004 && sheet.disc == 2 && sheet.files.size() == 1);
    CHECK(sheet.files[0].audio == dr::fs::path(L"C:\\music\\disc.flac"));
    const auto &tracks = sheet.files[0].tracks;
    CHECK(tracks.size() == 3 && tracks[0].start == 0 && tracks[0].end == 5 && tracks[1].performer == "Jay" && tracks[1].end == 62 + 37 / 75.0 && tracks[2].end == 0);
    // GBK sheets (what most Chinese rippers write) are read as text, not rejected.
    std::wstring wide_sheet = L"TITLE \"七里香\"\r\nFILE \"a.wav\" WAVE\r\nTRACK 01 AUDIO\r\nTITLE \"借口\"\r\nINDEX 01 00:00:00\r\n";
    std::string gbk(WideCharToMultiByte(936, 0, wide_sheet.data(), static_cast<int>(wide_sheet.size()), nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(936, 0, wide_sheet.data(), static_cast<int>(wide_sheet.size()), gbk.data(), static_cast<int>(gbk.size()), nullptr, nullptr);
    CHECK(dr::parse_cue(dr::cue_text(gbk), L".").files.at(0).tracks.at(0).title == "借口");
    // Imported first as one long file, then with its sheet: the file is replaced by the sheet's songs.
    const auto music = root / L"cue-music"; dr::fs::create_directories(music);
    wave(music / L"disc.wav"); // 16 s
    dr::Library library(root / L"cue-data"); library.load(); library.import_paths({music});
    const auto whole = library.snapshot()["tracks"].at(0)["id"].get<std::string>();
    library.update({{"favorites", {whole}}});
    // The sheet names the rip's original .flac; the .wav next to it is used.
    write(music / L"disc.cue", "PERFORMER \"Band\"\nTITLE \"Live\"\nFILE \"disc.flac\" WAVE\nTRACK 01 AUDIO\nTITLE \"One\"\nINDEX 01 00:00:00\nTRACK 02 AUDIO\nTITLE \"Two\"\nINDEX 01 00:05:00\nTRACK 03 AUDIO\nTITLE \"Three\"\nINDEX 01 00:10:00\n");
    auto result = library.import_paths({music});
    CHECK(result["imported"] == 3 && result["removed"] == 1);
    auto state = library.snapshot(); CHECK(state["tracks"].size() == 3 && state["favorites"].empty());
    for (const auto &candidate : result["pending"]) library.enrich_metadata(candidate);
    std::map<std::string, dr::Json> by_title;
    const auto enriched = library.snapshot();
    for (const auto &t : enriched["tracks"]) by_title[t["title"].get<std::string>()] = t;
    CHECK(by_title.contains("One") && by_title.contains("Two") && by_title.contains("Three")); // the file's tags do not overwrite the sheet
    CHECK(by_title["Two"]["artist"] == "Band" && by_title["Two"]["album"] == "Live" && by_title["Two"]["trackNumber"] == 2);
    CHECK(by_title["Two"]["cue"]["start"] == 5.0 && by_title["Two"]["cue"]["end"] == 10.0 && by_title["Two"]["duration"] == 5.0);
    CHECK(std::abs(by_title["Three"]["duration"].get<double>() - 6) < .1); // the last song runs to the end of the file
    CHECK(dr::path_key(library.audio_path(by_title["One"]["id"].get<std::string>())) == dr::path_key(music / L"disc.wav"));
    // A song's lyric file is named after its title.
    write(music / L"Two.lrc", "[00:00.00]第二首");
    CHECK(library.lyrics(by_title["Two"]["id"].get<std::string>())["text"] == "[00:00.00]第二首" && library.lyrics(by_title["One"]["id"].get<std::string>())["text"] == "");
    // Rescans keep the songs (same ids); without the sheet the file is one song again.
    const auto two = by_title["Two"]["id"].get<std::string>();
    CHECK(library.import_paths({music}, {}, {}, true)["removed"] == 0);
    CHECK(library.snapshot()["tracks"].size() == 3 && library.track(two)["title"] == "Two");
    dr::fs::remove(music / L"disc.cue");
    CHECK(library.import_paths({music}, {}, {}, true)["removed"] == 3);
    CHECK(library.snapshot()["tracks"].size() == 1 && !library.snapshot()["tracks"][0].contains("cue"));
    // 修改日期 sorting reads each file's time; 歌曲排序 / 歌曲名称 / 歌词源顺序 are validated settings.
    CHECK(library.snapshot()["tracks"][0].value("modifiedAt", 0LL) > 0);
    library.update({{"settings", {{"librarySort", "modified"}, {"titleSource", "file"}, {"lyricProviderOrder", {"qq", "bogus", "qq"}}, {"lyricSource", "ordered"}}}});
    library.update({{"settings", {{"librarySort", "random"}, {"titleSource", "tags"}}}});
    const auto saved = library.settings();
    CHECK(saved["librarySort"] == "modified" && saved["titleSource"] == "file" && saved["lyricSource"] == "ordered");
    CHECK(saved["lyricProviderOrder"][0] == "qq" && saved["lyricProviderOrder"].size() == dr::OnlineLyrics::provider_ids().size());
}// 哔哩哔哩 Index-Translate: call_api.py's request (one user message, temperature 0, thinking off),
// the chosen model, lines kept one per line and cached per model, errors surfaced, its host only.
void index_translate_test(const dr::fs::path &root) {
    CHECK(dr::IndexTranslate::prompt("  你好 ", "zh", "en") == "请将以下中文文本翻译为英语，直接输出翻译结果，不要进行任何解释。\n\n你好");
    CHECK(dr::IndexTranslate::prompt("夜", "ja", "zh") == "请将以下日语文本翻译为中文，直接输出翻译结果，不要进行任何解释。\n\n夜");
    CHECK(dr::IndexTranslate::strip_think("<think>\nhmm\n</think>\n\nHello") == "Hello" && dr::IndexTranslate::strip_think(" <think> Hi ") == "Hi");
    CHECK(dr::IndexTranslate::valid_model("Index-Translate-9B") && !dr::IndexTranslate::valid_model("hf.co/tencent/Hy-MT2-1.8B-GGUF:Q4_K_M"));
    int requests = 0; std::string fail; std::vector<std::string> models;
    auto fake = [&](const dr::HttpRequest &r) -> dr::HttpResponse {
        ++requests;
        CHECK(r.method == "POST" && r.url == "https://index-translate.bilibili.com/v1/chat/completions" && !r.direct);
        const auto body = dr::Json::parse(r.body);
        models.push_back(body["model"].get<std::string>());
        CHECK(body["temperature"] == 0.0 && body["stream"] == false && body["chat_template_kwargs"]["enable_thinking"] == false);
        CHECK(body["messages"].size() == 1 && body["messages"][0]["role"] == "user");
        if (fail == "429") return {429, R"({"error":{"message":"rate limited"}})"};
        if (fail == "500") return {500, R"({"error":{"message":"model overloaded"}})"};
        if (fail == "empty") return {200, R"({"choices":[]})"};
        auto text = body["messages"][0]["content"].get<std::string>();
        CHECK(text.starts_with("请将以下中文文本翻译为英语"));
        text = text.substr(text.find("\n\n") + 2);
        if (text.find("合并") != std::string::npos && text.find('\n') != std::string::npos) return {200, R"({"choices":[{"message":{"content":"merged into one line"}}]})"};
        std::string out; std::istringstream in(text);
        for (std::string line; std::getline(in, line);) out += (out.empty() ? "" : "\n") + ("EN " + line);
        return {200, dr::Json{{"choices", {{{"message", {{"role", "assistant"}, {"content", "<think>\n\n</think>\n\n" + out}}}}}}}.dump()};
    };
    dr::IndexTranslate translator(root / L"translations-index", fake);
    CHECK(translator.model() == "Index-Translate-2B"); // the default
    CHECK(translator.translate({"第一句", "", "第二句", "第一句"}, "zh", "en") == std::vector<std::string>({"EN 第一句", "", "EN 第二句", "EN 第一句"}));
    CHECK(requests == 1 && models.back() == "Index-Translate-2B"); // one request, repeated lines translated once
    CHECK(translator.translate({"第二句"}, "zh", "en") == std::vector<std::string>({"EN 第二句"}) && requests == 1); // cached
    dr::IndexTranslate reopened(root / L"translations-index", fake);
    CHECK(reopened.translate({"第一句"}, "zh", "en") == std::vector<std::string>({"EN 第一句"}) && requests == 1); // the cache survives a restart
    requests = 0;
    CHECK(translator.translate({"合并一", "合并二"}, "zh", "en") == std::vector<std::string>({"EN 合并一", "EN 合并二"}));
    CHECK(requests == 3); // a merged answer is retried line by line
    translator.set_model("Index-Translate-35B-A3B");
    CHECK(translator.translate({"第一句"}, "zh", "en") == std::vector<std::string>({"EN 第一句"}) && models.back() == "Index-Translate-35B-A3B"); // each model has its own cache
    translator.set_model("no-such-model");
    CHECK(translator.model() == "Index-Translate-2B");
    for (const auto *code : {"429", "500", "empty"}) {
        fail = code;
        try { translator.translate_text("你好", "zh", "en"); CHECK(false); }
        catch (const std::exception &e) { CHECK(std::string(e.what()).find("哔哩哔哩翻译") != std::string::npos); if (fail == "500") CHECK(std::string(e.what()).find("model overloaded") != std::string::npos); }
    }
    // Translation settings from the local Ollama model become Index-Translate with its default model.
    const auto data = root / L"translate-migration"; dr::fs::create_directory(data);
    write(data / L"library.json", R"({"version":1,"tracks":[],"folders":[],"favorites":[],"playlists":[],"settings":{"translateProvider":"local","translateEndpoint":"http://127.0.0.1:11434","translateModel":"hf.co/tencent/Hy-MT2-1.8B-GGUF:Q4_K_M"}})");
    dr::Library migrated(data); migrated.load();
    const auto settings = migrated.settings();
    CHECK(settings["translateProvider"] == "bilibili" && settings["translateModel"] == "Index-Translate-2B" && !settings.contains("translateEndpoint"));
    migrated.update({{"settings", {{"translateModel", "Index-Translate-9B"}, {"translateProvider", "niutrans"}}}});
    CHECK(migrated.settings()["translateModel"] == "Index-Translate-9B" && migrated.settings()["translateProvider"] == "niutrans");
    migrated.update({{"settings", {{"translateModel", "llama3"}, {"translateProvider", "local"}}}});
    CHECK(migrated.settings()["translateModel"] == "Index-Translate-9B" && migrated.settings()["translateProvider"] == "niutrans"); // invalid values are ignored
    // The real client refuses any other host before connecting.
    auto real = dr::winhttp_fetch([](const std::string &host) { return host == dr::IndexTranslate::kHost; });
    CHECK(throws([&] { real({"POST", "https://example.com/v1/chat/completions"}); }));
    // Closing the app cancels a request the server is still answering, instead of waiting for its timeout.
    WSADATA wsa{}; CHECK(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int length = sizeof(address);
    CHECK(server != INVALID_SOCKET && bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0 && listen(server, 4) == 0
          && getsockname(server, reinterpret_cast<sockaddr *>(&address), &length) == 0); // accepted by the backlog, never answered
    auto cancel = std::make_shared<dr::HttpCancel>();
    auto slow = dr::winhttp_fetch([](const std::string &host) { return host == "127.0.0.1"; }, cancel);
    dr::HttpRequest hanging{"POST", "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/v1/chat/completions"};
    hanging.direct = true; hanging.timeout_ms = 3 * 60 * 1000;
    const auto started = std::chrono::steady_clock::now();
    auto pending = std::async(std::launch::async, [&] { return throws([&] { slow(hanging); }); });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    cancel->cancel();
    CHECK(pending.wait_for(std::chrono::seconds(5)) == std::future_status::ready && pending.get());
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
    CHECK(throws([&] { slow(hanging); })); // later requests fail at once
    closesocket(server); WSACleanup();
    // Opt-in live check against the public API: DR_MUSIC_NETWORK_TEST=1.
    char *network = nullptr;
    if (_dupenv_s(&network, nullptr, "DR_MUSIC_NETWORK_TEST") == 0 && network && std::string(network) == "1") {
        dr::IndexTranslate live(root / L"translations-index-live", real);
        const auto out = live.translate({"故事的小黄花", "从出生那年就飘着"}, "zh", "en");
        std::cout << "live index-translate: " << out[0] << " / " << out[1] << "\n";
        CHECK(out.size() == 2 && !out[0].empty() && !out[1].empty());
    }
    free(network);
}
// 小牛翻译 v2: the documented authStr (MD5 of sorted key=value pairs incl. apikey), one JSON POST to
// api.niutrans.com only, error codes surfaced, lines kept one per line and cached.
void niutrans_test(const dr::fs::path &root) {
    CHECK(dr::NiuTrans::auth_str({{"from", "zh"}, {"to", "en"}, {"srcText", "你好"}, {"appId", "app123"}, {"timestamp", "1700000000000"}}, "key456") == "7b63ea8c434003c0dfb6e0ef4e0f6a46"); // node crypto reference
    CHECK(dr::NiuTrans::auth_str({{"from", "zh"}, {"to", "en"}, {"srcText", "你好"}, {"appId", "app123"}, {"timestamp", "1700000000000"}, {"termLibraryId", ""}, {"authStr", "x"}}, "key456") == "7b63ea8c434003c0dfb6e0ef4e0f6a46"); // empty / authStr skipped
    int requests = 0; std::string fail;
    auto fake = [&](const dr::HttpRequest &r) -> dr::HttpResponse {
        ++requests;
        CHECK(r.method == "POST" && r.url == "https://api.niutrans.com/v2/text/translate");
        const auto body = dr::Json::parse(r.body);
        CHECK(body["appId"] == "app123" && body["timestamp"] == "1700000000000");
        CHECK(body["authStr"] == dr::NiuTrans::auth_str({{"from", body["from"]}, {"to", body["to"]}, {"srcText", body["srcText"]}, {"appId", "app123"}, {"timestamp", "1700000000000"}}, "key456"));
        if (!fail.empty()) return {200, dr::Json{{"errorCode", fail}, {"errorMsg", "x"}, {"from", "zh"}, {"to", "en"}}.dump()};
        std::string out; std::istringstream in(body["srcText"].get<std::string>());
        for (std::string line; std::getline(in, line);) out += (out.empty() ? "" : "\n") + ("EN " + line + " ");
        return {200, dr::Json{{"from", "zh"}, {"to", "en"}, {"tgtText", out}}.dump()};
    };
    dr::NiuTrans niutrans(root / L"translations-niutrans", fake, [] { return 1700000000000LL; });
    CHECK(!niutrans.configured() && throws([&] { niutrans.translate_text("你好", "zh", "en"); }) && requests == 0);
    niutrans.set_credentials("app123", "key456");
    CHECK(niutrans.configured() && niutrans.app_id() == "app123");
    CHECK(niutrans.translate({"第一句", "", "第二句", "第一句"}, "zh", "en") == std::vector<std::string>({"EN 第一句", "", "EN 第二句", "EN 第一句"}));
    CHECK(requests == 1); // both lines in one request
    CHECK(niutrans.translate({"第二句"}, "zh", "en") == std::vector<std::string>({"EN 第二句"}) && requests == 1); // cached
    fail = "20001";
    try { niutrans.translate_text("你好", "zh", "en"); CHECK(false); } catch (const std::exception &e) { CHECK(std::string(e.what()).find("App ID") != std::string::npos); }
    auto real = dr::winhttp_fetch([](const std::string &host) { return host == dr::NiuTrans::kHost; });
    CHECK(throws([&] { real({"GET", "https://example.com/v2/text/translate"}); }));
    // Opt-in live check with the developer's own key: DR_MUSIC_NIUTRANS_APPID / DR_MUSIC_NIUTRANS_APIKEY.
    char *app = nullptr, *key = nullptr;
    if (_dupenv_s(&app, nullptr, "DR_MUSIC_NIUTRANS_APPID") == 0 && app && _dupenv_s(&key, nullptr, "DR_MUSIC_NIUTRANS_APIKEY") == 0 && key) {
        dr::NiuTrans live(root / L"translations-live", real);
        live.set_credentials(app, key);
        const auto out = live.translate({"故事的小黄花", "从出生那年就飘着"}, "zh", "en");
        std::cout << "live niutrans: " << out[0] << " / " << out[1] << "\n";
        CHECK(out.size() == 2 && !out[0].empty() && !out[1].empty());
    }
    free(app); free(key);
}
// Lyric providers added in a later version start enabled; ones the user switched off stay off.
void provider_upgrade_test(const dr::fs::path &root) {
    auto data = root / L"provider-upgrade"; dr::fs::create_directory(data);
    auto saved = dr::Library::defaults(); saved["settings"]["lyricProviders"] = {"lrclib", "qq"}; saved["settings"].erase("lyricProvidersSeen");
    write(data / L"library.json", saved.dump());
    dr::Library legacy(data); CHECK(legacy.load()["settings"]["lyricProviders"] == dr::Json({"lrcred", "qq", "lrclib"}));
    legacy.update({{"settings", {{"lyricProviders", {"lrclib"}}}}});
    dr::Library later(data); CHECK(later.load()["settings"]["lyricProviders"] == dr::Json({"lrclib"}));
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
    try { media_test(root); fonts_test(); chinese_test(); rescan_test(root); lyric_crypto_test(); online_lyrics_test(root); library_test(root); deferred_metadata_test(root); bulk_remove_test(root); recovery_test(root); provider_upgrade_test(root); index_translate_test(root); cue_test(root); niutrans_test(root); caption_test(); caption_leave_test(); webview_caption_test(); std::cout << "PASS: native metadata/library/persistence/lyrics, system fonts and lyric typography settings, OpenCC simplified/traditional folding, rescan pruning of moved/deleted files, online TTML matching, bounded media streaming, ranges, D2D caption hit tests, leave-message regression, WebView2 caption hit testing and switching\n"; }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; code = 1; }
    dr::fs::remove_all(root); CoUninitialize(); return code;
}
