#pragma once
#include "common.h"
#include <memory>
namespace dr {
class AudioDecoder {
public:
    static constexpr unsigned rate = 48000, channels = 2;
    explicit AudioDecoder(const fs::path &file);
    ~AudioDecoder();
    /** Of the range, when one is set. */
    double duration() const;
    size_t read(float *output, size_t frames);
    /** Seconds from the start of the range. */
    void seek(double seconds);
    /** Plays only [start, end) of the file (a CUE track; end 0 = to the end): duration, read and seek become relative to it. */
    void set_range(double start, double end);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
