#pragma once
#include "common.h"
#include <functional>
#include <memory>
namespace dr {
class SystemMedia {
public:
    SystemMedia();
    ~SystemMedia();
    void initialize(HWND window, std::function<void(Json)> action);
    void update(const Json &state, const Json &track);
    void shutdown();
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
