#include "decoder.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
namespace dr {
namespace {
void checked(int status, const char *operation) {
    if (status >= 0) return;
    char message[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(status, message, sizeof(message));
    throw std::runtime_error(std::string(operation) + ": " + message);
}
}
struct AudioDecoder::Impl {
    FILE *file = nullptr;
    AVIOContext *io = nullptr;
    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    SwrContext *resampler = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    int stream = -1;
    bool input_end = false, decoder_end = false;
    int damaged = 0; // consecutive undecodable packets / frames
    double length = 0, discard_until = -1;
    std::vector<float> samples;
    size_t offset = 0;
    ~Impl() {
        av_frame_free(&frame); av_packet_free(&packet); swr_free(&resampler);
        avcodec_free_context(&codec); avformat_close_input(&format);
        if (io) { av_freep(&io->buffer); avio_context_free(&io); }
        if (file) fclose(file);
    }
    static int read_file(void *opaque, uint8_t *buffer, int size) {
        auto file = static_cast<FILE *>(opaque); const auto count = fread(buffer, 1, size, file);
        return count ? static_cast<int>(count) : ferror(file) ? AVERROR(EIO) : AVERROR_EOF;
    }
    static int64_t seek_file(void *opaque, int64_t offset, int origin) {
        auto file = static_cast<FILE *>(opaque);
        if (origin == AVSEEK_SIZE) {
            const auto position = _ftelli64(file);
            if (_fseeki64(file, 0, SEEK_END)) return AVERROR(EIO);
            const auto size = _ftelli64(file); if (_fseeki64(file, position, SEEK_SET)) return AVERROR(EIO); return size;
        }
        if (_fseeki64(file, offset, origin & ~AVSEEK_FORCE)) return AVERROR(EIO);
        return _ftelli64(file);
    }
    bool next() {
        samples.clear(); offset = 0;
        while (!decoder_end) {
            const int received = avcodec_receive_frame(codec, frame);
            if (received >= 0) {
                const auto count = swr_get_out_samples(resampler, frame->nb_samples);
                samples.resize(static_cast<size_t>(count) * channels);
                uint8_t *output = reinterpret_cast<uint8_t *>(samples.data());
                const auto converted = swr_convert(resampler, &output, count, const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
                checked(converted, "音频重采样失败"); samples.resize(static_cast<size_t>(converted) * channels);
                if (discard_until >= 0 && frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                    const auto source = format->streams[stream];
                    const auto start = source->start_time == AV_NOPTS_VALUE ? 0 : source->start_time;
                    const double time = (frame->best_effort_timestamp - start) * av_q2d(source->time_base);
                    const auto skip = static_cast<size_t>(std::max(0.0, std::ceil((discard_until - time) * rate)));
                    offset = std::min(samples.size(), skip * channels);
                    if (offset < samples.size()) discard_until = -1;
                }
                av_frame_unref(frame);
                if (offset < samples.size()) return true;
                continue;
            }
            if (received == AVERROR_EOF) { decoder_end = true; break; }
            // Damaged data (often a truncated or corrupt last frame) is skipped, as ffmpeg and other
            // players do, instead of ending playback with an error; a stream that stays undecodable
            // is treated as ended.
            if (received == AVERROR_INVALIDDATA) { if (++damaged > 64) { decoder_end = true; break; } continue; }
            checked(received == AVERROR(EAGAIN) ? 0 : received, "音频解码失败");
            if (input_end) throw std::runtime_error("音频解码器未完成刷新");
            int read = 0;
            do { av_packet_unref(packet); read = av_read_frame(format, packet); } while (read >= 0 && packet->stream_index != stream);
            if (read == AVERROR_EOF) { input_end = true; checked(avcodec_send_packet(codec, nullptr), "无法刷新音频解码器"); }
            else if (read == AVERROR_INVALIDDATA) { if (++damaged > 64) { input_end = true; checked(avcodec_send_packet(codec, nullptr), "无法刷新音频解码器"); } }
            else {
                checked(read, "无法读取音频数据");
                const int sent = avcodec_send_packet(codec, packet);
                if (sent == AVERROR_INVALIDDATA) { if (++damaged > 64) { input_end = true; checked(avcodec_send_packet(codec, nullptr), "无法刷新音频解码器"); } }
                else { checked(sent, "无法提交音频数据"); damaged = 0; }
            }
        }
        const int available = swr_get_out_samples(resampler, 0);
        if (available <= 0) return false;
        samples.resize(static_cast<size_t>(available) * channels);
        uint8_t *output = reinterpret_cast<uint8_t *>(samples.data());
        const auto converted = swr_convert(resampler, &output, available, nullptr, 0);
        checked(converted, "无法刷新重采样器"); samples.resize(static_cast<size_t>(converted) * channels);
        return !samples.empty();
    }
};
AudioDecoder::AudioDecoder(const fs::path &file) : impl_(std::make_unique<Impl>()) {
    auto &p = *impl_; _wfopen_s(&p.file, file.c_str(), L"rb");
    if (!p.file) throw std::runtime_error("无法打开音频文件");
    auto buffer = static_cast<unsigned char *>(av_malloc(32768)); if (!buffer) throw std::bad_alloc();
    p.io = avio_alloc_context(buffer, 32768, 0, p.file, Impl::read_file, nullptr, Impl::seek_file);
    if (!p.io) { av_free(buffer); throw std::bad_alloc(); }
    p.format = avformat_alloc_context(); if (!p.format) throw std::bad_alloc();
    p.format->pb = p.io; p.format->flags |= AVFMT_FLAG_CUSTOM_IO;
    checked(avformat_open_input(&p.format, nullptr, nullptr, nullptr), "无法识别音频容器");
    checked(avformat_find_stream_info(p.format, nullptr), "无法读取音频流");
    p.stream = av_find_best_stream(p.format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0); checked(p.stream, "没有可播放的音频流");
    auto source = p.format->streams[p.stream]; auto decoder = avcodec_find_decoder(source->codecpar->codec_id);
    if (!decoder) throw std::runtime_error("不支持此音频编码");
    p.codec = avcodec_alloc_context3(decoder); if (!p.codec) throw std::bad_alloc();
    checked(avcodec_parameters_to_context(p.codec, source->codecpar), "无法配置音频解码器");
    p.codec->thread_count = 1; checked(avcodec_open2(p.codec, decoder, nullptr), "无法启动音频解码器");
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    checked(swr_alloc_set_opts2(&p.resampler, &stereo, AV_SAMPLE_FMT_FLT, rate, &p.codec->ch_layout, p.codec->sample_fmt, p.codec->sample_rate, 0, nullptr), "无法配置重采样器");
    checked(swr_init(p.resampler), "无法启动重采样器");
    p.packet = av_packet_alloc(); p.frame = av_frame_alloc(); if (!p.packet || !p.frame) throw std::bad_alloc();
    if (source->duration != AV_NOPTS_VALUE) p.length = source->duration * av_q2d(source->time_base);
    else if (p.format->duration != AV_NOPTS_VALUE) p.length = static_cast<double>(p.format->duration) / AV_TIME_BASE;
}
AudioDecoder::~AudioDecoder() = default;
double AudioDecoder::duration() const { return impl_->length; }
size_t AudioDecoder::read(float *output, size_t frames) {
    auto &p = *impl_; size_t copied = 0;
    while (copied < frames * channels) {
        if (p.offset == p.samples.size() && !p.next()) break;
        const auto count = std::min(frames * channels - copied, p.samples.size() - p.offset);
        std::memcpy(output + copied, p.samples.data() + p.offset, count * sizeof(float)); p.offset += count; copied += count;
    }
    return copied / channels;
}
void AudioDecoder::seek(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 7 * 24 * 3600) throw std::runtime_error("无效的音频定位");
    auto &p = *impl_; auto stream = p.format->streams[p.stream];
    const auto start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    const auto target = static_cast<int64_t>(seconds / av_q2d(stream->time_base)) + start;
    checked(avformat_seek_file(p.format, p.stream, INT64_MIN, target, target, AVSEEK_FLAG_BACKWARD), "无法定位音频");
    avcodec_flush_buffers(p.codec); swr_close(p.resampler); checked(swr_init(p.resampler), "无法重置重采样器");
    p.samples.clear(); p.offset = 0; p.input_end = p.decoder_end = false; p.damaged = 0; p.discard_until = seconds;
}
}
