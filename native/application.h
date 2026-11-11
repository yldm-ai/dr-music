#pragma once
#include "library.h"
#include "window.h"
#include "webview.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <thread>
namespace dr {
class Application {
public:
    Application(fs::path data_root, fs::path assets, unsigned debug_port, std::vector<fs::path> imports, const std::string &shell);
    ~Application();
    int run(HINSTANCE instance);
private:
    void enqueue(std::function<void(std::stop_token)> task);
    void receive(const Json &request);
    void reply(const Json &id, Json result);
    void reject(const Json &id, const std::string &error);
    std::vector<fs::path> choose_files(const std::string &kind);
    Window window_;
    Library library_;
    WebView webview_;
    fs::path assets_;
    unsigned debug_port_;
    std::vector<fs::path> imports_;
    std::string shell_;
    std::atomic<bool> closing_ = false;
    bool importing_ = false;
    std::mutex jobs_mutex_;
    std::condition_variable_any ready_;
    std::deque<std::function<void(std::stop_token)>> jobs_;
    std::jthread worker_;
};
}
