#include "playback.h"
#include "decoder.h"
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <thread>
#include <cmath>
namespace dr {
struct Playback::Impl {
    static constexpr size_t capacity = AudioDecoder::rate * 2;
    std::vector<float> ring = std::vector<float>(capacity * 2);
    std::atomic<uint64_t> read_position{0}, write_position{0};
    std::atomic<float> volume{0.7f};
    std::mutex mutex;
    std::condition_variable_any ready;
    std::deque<std::function<void()>> commands;
    std::function<void(Json)> listener;
    std::unique_ptr<AudioDecoder> decoder;
    ma_context context{}; ma_device device{};
    bool context_ready = false, device_ready = false, playing = false, ended = false, eof = false;
    bool use_null;
    double base = 0, duration = 0;
    std::string id, error, session;
    uint64_t request = 0, serial = 0;
    std::chrono::steady_clock::time_point drain_started{};
    std::jthread worker;
    explicit Impl(bool null_device) : use_null(null_device), worker([this](std::stop_token stop) { run(stop); }) {}
    ~Impl() { worker.request_stop(); ready.notify_all(); worker.join(); }
    static void output(ma_device *device, void *output, const void *, ma_uint32 frames) {
        auto &p = *static_cast<Impl *>(device->pUserData); auto target = static_cast<float *>(output);
        const auto read = p.read_position.load(std::memory_order_relaxed);
        const auto count = std::min<uint64_t>(frames, p.write_position.load(std::memory_order_acquire) - read);
        const auto gain = p.volume.load(std::memory_order_relaxed);
        for (uint64_t i = 0; i < count; ++i) for (unsigned channel = 0; channel < 2; ++channel)
            target[i * 2 + channel] = p.ring[((read + i) % capacity) * 2 + channel] * gain;
        std::fill(target + count * 2, target + frames * 2, 0.0f);
        p.read_position.store(read + count, std::memory_order_release);
    }
    Json state() {
        const auto position = base + static_cast<double>(read_position.load()) / AudioDecoder::rate;
        return {{"id", id}, {"position", duration > 0 ? std::min(duration, position) : position}, {"duration", duration},
            {"playing", playing}, {"ended", ended}, {"volume", volume.load()}, {"error", error}, {"request", request}, {"serial", ++serial}, {"session", session}, {"backend", device_ready ? ma_get_backend_name(context.backend) : ""}};
    }
    void publish() {
        auto value = state(); std::function<void(Json)> callback;
        { std::lock_guard lock(mutex); callback = listener; }
        if (callback) callback(std::move(value));
    }
    void pause() { if (device_ready) ma_device_stop(&device); playing = false; drain_started = {}; }
    void reset() { pause(); decoder.reset(); id.clear(); duration = base = 0; read_position = 0; write_position = 0; eof = ended = false; error.clear(); }
    void device_init() {
        if (device_ready) return;
        const ma_backend backend = ma_backend_null;
        const ma_backend windows_backends[]{ma_backend_wasapi, ma_backend_dsound, ma_backend_winmm};
        if (ma_context_init(use_null ? &backend : windows_backends, use_null ? 1 : 3, nullptr, &context) != MA_SUCCESS) throw std::runtime_error("无法初始化音频输出");
        context_ready = true;
        auto config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32; config.playback.channels = 2; config.sampleRate = AudioDecoder::rate;
        config.dataCallback = output; config.pUserData = this;
        config.periodSizeInMilliseconds = 10;
        if (ma_device_init(&context, &config, &device) != MA_SUCCESS) {
            ma_context_uninit(&context); context_ready = false; throw std::runtime_error("无法打开音频输出设备");
        }
        device_ready = true;
    }
    void fill() {
        if (!decoder || eof) return;
        const auto write = write_position.load(std::memory_order_relaxed);
        const auto available = capacity - (write - read_position.load(std::memory_order_acquire));
        const auto count = std::min<uint64_t>({available, capacity - write % capacity, 4096});
        if (!count) return;
        const auto got = decoder->read(ring.data() + (write % capacity) * 2, static_cast<size_t>(count));
        write_position.store(write + got, std::memory_order_release);
        if (!got) eof = true;
    }
    Json execute(const Json &message, const fs::path &file) {
        const auto action = message.at("action").get<std::string>();
        const auto token = message.at("request").get<uint64_t>();
        const auto client = message.value("session", session);
        if (client != session) { session = client; request = 0; }
        if (token < request) return state();
        request = token; error.clear();
        if (action == "play") {
            const auto next = message.at("id").get<std::string>();
            if (next != id || !decoder) { reset(); decoder = std::make_unique<AudioDecoder>(file); id = next; duration = decoder->duration(); }
            else if (ended) { pause(); decoder->seek(0); read_position = 0; write_position = 0; base = 0; eof = ended = false; }
            device_init(); fill();
            if (ma_device_start(&device) != MA_SUCCESS) throw std::runtime_error("无法启动音频输出设备");
            playing = true;
        } else if (action == "pause") pause();
        else if (action == "stop") reset();
        else if (action == "seek") {
            const auto seconds = message.at("position").get<double>();
            if (!std::isfinite(seconds) || seconds < 0 || seconds > 7 * 24 * 3600) throw std::runtime_error("无效的播放位置");
            if (decoder) {
                const bool resume = playing; pause(); base = duration > 0 ? std::min(seconds, duration) : seconds;
                decoder->seek(base); read_position = 0; write_position = 0; eof = ended = false; fill();
                if (resume) { if (ma_device_start(&device) != MA_SUCCESS) throw std::runtime_error("无法恢复音频输出"); playing = true; }
            }
        } else if (action == "volume") {
            const auto value = message.at("volume").get<double>();
            if (!std::isfinite(value) || value < 0 || value > 1) throw std::runtime_error("无效的音量");
            volume = static_cast<float>(value);
        } else if (action != "state") throw std::runtime_error("不支持的播放操作");
        publish(); return state();
    }
    void run(std::stop_token stop) {
        auto last = std::chrono::steady_clock::now();
        while (!stop.stop_requested()) {
            std::function<void()> command;
            { std::unique_lock lock(mutex); if (commands.empty()) ready.wait_for(lock, stop, std::chrono::milliseconds(5), [this] { return !commands.empty(); });
              if (!commands.empty()) { command = std::move(commands.front()); commands.pop_front(); } }
            if (command) command();
            try {
                if (playing) {
                    fill();
                    if (eof && read_position.load() == write_position.load()) {
                        // The last callback has submitted, not necessarily played,
                        // the last frames. Allow the short device buffers to drain.
                        const auto now = std::chrono::steady_clock::now();
                        if (drain_started == std::chrono::steady_clock::time_point{}) drain_started = now;
                        else if (now - drain_started >= std::chrono::milliseconds(50)) { pause(); ended = true; publish(); }
                    }
                }
            } catch (const std::exception &e) { pause(); error = e.what(); publish(); }
            const auto now = std::chrono::steady_clock::now();
            if (playing && now - last >= std::chrono::milliseconds(100)) { publish(); last = now; }
        }
        reset(); if (device_ready) ma_device_uninit(&device); if (context_ready) ma_context_uninit(&context);
    }
};
Playback::Playback(bool null_device) : impl_(std::make_unique<Impl>(null_device)) {}
Playback::~Playback() = default;
void Playback::set_listener(std::function<void(Json)> listener) { std::lock_guard lock(impl_->mutex); impl_->listener = std::move(listener); }
Json Playback::command(const Json &request, const fs::path &file) {
    auto promise = std::make_shared<std::promise<Json>>(); auto future = promise->get_future();
    { std::lock_guard lock(impl_->mutex); impl_->commands.push_back([this, request, file, promise] {
        try { promise->set_value(impl_->execute(request, file)); }
        catch (const std::exception &e) { impl_->pause(); impl_->error = e.what(); impl_->publish(); promise->set_exception(std::current_exception()); }
    }); }
    impl_->ready.notify_one(); return future.get();
}
}
