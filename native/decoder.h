#pragma once
#include "common.h"
#include <memory>
namespace dr {
class AudioDecoder {
public:
    static constexpr unsigned rate = 48000, channels = 2;
    explicit AudioDecoder(const fs::path &file);
    ~AudioDecoder();
    double duration() const;
    size_t read(float *output, size_t frames);
    void seek(double seconds);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
