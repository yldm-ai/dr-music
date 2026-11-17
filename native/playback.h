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
    /** For "play": a CUE track's range of the file in seconds (end 0 = to the end of the file). */
    Json command(const Json &request, const fs::path &file = {}, double start = 0, double end = 0);
    void set_listener(std::function<void(Json)> listener);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
