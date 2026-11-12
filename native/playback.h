#pragma once
#include "common.h"
#include <functional>
#include <memory>
namespace dr {
// All decode/device state lives on an independent native worker. Commands
// accept only a library-resolved local path, never a WebView-provided URL.
class Playback {
public:
    explicit Playback(bool null_device = false);
    ~Playback();
    Playback(const Playback &) = delete;
    Playback &operator=(const Playback &) = delete;
    Json command(const Json &request, const fs::path &file = {});
    void set_listener(std::function<void(Json)> listener);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
