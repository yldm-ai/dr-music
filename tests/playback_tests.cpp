#include "decoder.h"
#include "playback.h"
#include <thread>
#include <iostream>
#include <cmath>
#include <fstream>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#define CHECK(value) do { if (!(value)) throw std::runtime_error("Playback check failed at line " + std::to_string(__LINE__) + ": " #value); } while (false)
using namespace std::chrono_literals;
template<class F> bool throws(F fn) { try { fn(); return false; } catch (const std::exception &) { return true; } }
int wmain(int argc, wchar_t **argv) {
    if (argc != 2) return 2;
    try {
        const dr::fs::path root(argv[1]);
        for (const auto *name : {L"pcm.wav", L"float.wav", L"music.mp3", L"music.flac", L"music.aac", L"music.m4a", L"lossless.m4a", L"music.ogg", L"music.opus", L"music.webm"}) {
            dr::AudioDecoder decoder(root / name); CHECK(decoder.duration() > 1.8 && decoder.duration() < 2.3);
            float pcm[2048]{}; double energy = 0; size_t total = 0, got = 0;
            while ((got = decoder.read(pcm, 1024)) != 0) { total += got; for (size_t i = 0; i < got * 2; ++i) { CHECK(std::isfinite(pcm[i])); energy += pcm[i] * pcm[i]; } CHECK(total < 120000); }
            CHECK(total > 85000 && total < 110000); CHECK(energy > 1);
            decoder.seek(.75); total = 0;
            while ((got = decoder.read(pcm, 1024)) != 0) { total += got; CHECK(total < 90000); }
            CHECK(total > 50000 && total < 70000);
            decoder.seek(0); CHECK(decoder.read(pcm, 1024) == 1024);
            std::cout << "PASS decode/seek: " << dr::path_text(name) << '\n';
        }
        // A CUE track: only [start, end) of the file, with duration, reads and seeks relative to it.
        {
            float pcm[2048]{}; size_t total = 0, got = 0;
            dr::AudioDecoder range(root / L"pcm.wav"); range.set_range(.5, 1.5); CHECK(std::abs(range.duration() - 1) < 1e-9);
            while ((got = range.read(pcm, 1024)) != 0) total += got;
            CHECK(total == 48000);
            range.seek(.5); total = 0; while ((got = range.read(pcm, 1024)) != 0) total += got;
            CHECK(total == 24000);
            dr::AudioDecoder tail(root / L"pcm.wav"); tail.set_range(1, 0); CHECK(tail.duration() > .8 && tail.duration() < 1.3);
            total = 0; while ((got = tail.read(pcm, 1024)) != 0) total += got;
            CHECK(total > 38000 && total < 62000);
            CHECK(throws([&] { tail.set_range(1, .5); }));
        }
        CHECK(throws([&] { dr::AudioDecoder invalid(root / L"broken.wav"); }));
        CHECK(throws([&] { dr::AudioDecoder invalid(root / L"missing.wav"); }));
        // The custom AVIO path must handle Unicode without URL/path guessing.
        dr::fs::copy_file(root / L"pcm.wav", root / L"中文 音乐.wav");
        { dr::AudioDecoder unicode(root / L"中文 音乐.wav"); CHECK(unicode.duration() > 1.9); }
        CHECK(!av_find_input_format("hls")); CHECK(!avcodec_find_decoder(AV_CODEC_ID_H264));
        CHECK(!avcodec_find_encoder(AV_CODEC_ID_AAC));
        CHECK(std::string(avcodec_configuration()).find("--disable-network") != std::string::npos);
        dr::Playback player(true); uint64_t request = 0;
        auto command = [&](const char *action, dr::Json extra = dr::Json::object()) { extra["action"] = action; extra["request"] = ++request; return player.command(extra, root / L"pcm.wav"); };
        auto playing = command("play", {{"id", "first"}}); CHECK(playing["playing"] == true);
        std::this_thread::sleep_for(200ms); auto position = command("state")["position"].get<double>(); CHECK(position > .05 && position < 1);
        const auto paused = command("pause"); std::this_thread::sleep_for(100ms);
        CHECK(command("state")["position"] == paused["position"]);
        auto seek = command("seek", {{"position", .75}}); CHECK(seek["position"] == .75); CHECK(seek["playing"] == false);
        CHECK(command("volume", {{"volume", .25}})["volume"] == .25);
        CHECK(command("play", {{"id", "first"}})["playing"] == true);
        command("seek", {{"position", 1.85}});
        bool ended = false;
        for (int i = 0; i < 100; ++i) { auto state = command("state"); if (state["ended"] == true) { ended = true; CHECK(state["playing"] == false); break; } std::this_thread::sleep_for(10ms); }
        CHECK(ended); CHECK(command("play", {{"id", "first"}})["position"].get<double>() < .1);
        command("play", {{"id", "second"}});
        const auto stale = player.command({{"action", "stop"}, {"request", 0ULL}}); CHECK(stale["id"] == "second");
        CHECK(command("stop")["id"] == ""); CHECK(command("state")["position"] == 0);
        CHECK(throws([&] { command("seek", {{"position", -1}}); }));
        CHECK(throws([&] { command("volume", {{"volume", 2}}); }));
        CHECK(throws([&] { command("unknown"); }));
        // The transport reports a CUE track's own length and position.
        const auto cue = player.command({{"action", "play"}, {"id", "cue-02"}, {"request", ++request}}, root / L"pcm.wav", .5, 1.5);
        CHECK(cue["duration"] == 1.0 && cue["position"].get<double>() < .1);
        CHECK(std::abs(command("seek", {{"position", .9}})["position"].get<double>() - .9) < .1);
        ended = false;
        for (int i = 0; i < 100; ++i) { auto state = command("state"); if (state["ended"] == true) { ended = true; CHECK(state["position"].get<double>() <= 1.0); break; } std::this_thread::sleep_for(10ms); }
        CHECK(ended);
        const auto reloaded = player.command({{"action", "play"}, {"id", "reloaded"}, {"request", 1ULL}, {"session", "new-main-page"}}, root / L"pcm.wav");
        CHECK(reloaded["id"] == "reloaded"); CHECK(reloaded["playing"] == true);
        std::cout << "PASS: native transport, clock, pause, seek, volume, EOF/replay, stale requests, local-only audio build\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
