#pragma once
#include "library.h"
#include "chinese.h"
#include "online_lyrics.h"
#include "index_translate.h"
#include "niutrans.h"
#include "window.h"
#include "webview.h"
#include "playback.h"
#include "system_media.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <thread>
#include <chrono>
namespace dr {
class Application {
public:
    Application(fs::path data_root, fs::path assets, unsigned debug_port, std::vector<fs::path> imports, const std::string &shell, fs::path online_lyrics_fixture = {}, std::string translate_api = {});
    ~Application();
    int run(HINSTANCE instance);
private:
    void enqueue(std::function<void(std::stop_token)> task, bool background = false);
    void enqueue_transport(std::function<void()> task);
    void enqueue_network(std::function<void()> task);
    void apply_lyric_options();
    void apply_translate_options();
    void enqueue_translate(std::function<void()> task);
    fs::path apple_token_file() const;
    void save_apple_token(const std::string &token);
    std::string load_apple_token() const;
    fs::path niutrans_key_file() const;
    void load_niutrans_key();
    static void seal(const fs::path &file, const std::string &secret, const wchar_t *description);
    static std::string unseal(const fs::path &file);
    std::vector<std::string> translate_lines(const std::vector<std::string> &lines, const std::string &source, const std::string &target);
    void schedule_metadata(Json candidates, size_t offset = 0);
    void receive(const Json &request);
    void reply(const Json &id, Json result);
    void reject(const Json &id, const std::string &error);
    std::vector<fs::path> choose_files(const std::string &kind);
    Window window_;
    Library library_;
    WebView webview_;
    Playback playback_;
    SystemMedia system_media_;
    Json media_track_;
    fs::path assets_;
    ChineseText chinese_;
    // Cancelled on close so the workers joined on exit (and the library lock) are released at once.
    std::shared_ptr<HttpCancel> http_cancel_ = std::make_shared<HttpCancel>();
    OnlineLyrics online_lyrics_;
    IndexTranslate bilibili_;
    NiuTrans niutrans_;
    unsigned debug_port_;
    std::vector<fs::path> imports_;
    std::string shell_;
    std::atomic<bool> closing_ = false;
    bool importing_ = false;
    std::mutex jobs_mutex_;
    std::condition_variable_any ready_;
    std::deque<std::function<void(std::stop_token)>> jobs_;
    std::deque<std::function<void(std::stop_token)>> background_jobs_;
    std::chrono::steady_clock::time_point metadata_saved_ = std::chrono::steady_clock::now();
    std::jthread worker_;
    std::mutex transport_mutex_;
    std::condition_variable_any transport_ready_;
    std::deque<std::function<void()>> transport_jobs_;
    std::jthread transport_worker_;
    // Online lyrics run on their own thread so slow downloads never hold up library work.
    std::mutex network_mutex_;
    std::condition_variable_any network_ready_;
    std::deque<std::function<void()>> network_jobs_;
    std::jthread network_worker_;
    // Lyric translation has its own queue: a long lyric must not delay online lyric lookups.
    std::mutex translate_mutex_;
    std::condition_variable_any translate_ready_;
    std::deque<std::function<void()>> translate_jobs_;
    std::jthread translate_worker_;
};
}
