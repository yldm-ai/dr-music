#include "system_media.h"
#include <systemmediatransportcontrolsinterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <mutex>
namespace dr {
using namespace winrt::Windows::Media;
struct SystemMedia::Impl {
    SystemMediaTransportControls controls{nullptr};
    winrt::event_token buttons{}, seeking{};
    std::function<void(Json)> action;
    std::mutex mutex;
    std::string signature;
};
SystemMedia::SystemMedia() : impl_(std::make_shared<Impl>()) {}
SystemMedia::~SystemMedia() { shutdown(); }
void SystemMedia::shutdown() {
    auto &p = *impl_;
    { std::lock_guard lock(p.mutex); p.action = {}; }
    if (p.controls) {
        try { p.controls.ButtonPressed(p.buttons); p.controls.PlaybackPositionChangeRequested(p.seeking); p.controls.IsEnabled(false); } catch (const winrt::hresult_error &) {}
        p.controls = nullptr;
    }
}
void SystemMedia::initialize(HWND window, std::function<void(Json)> action) {
    try {
        auto factory = winrt::get_activation_factory<SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
        winrt::check_hresult(factory->GetForWindow(window, winrt::guid_of<SystemMediaTransportControls>(), winrt::put_abi(impl_->controls)));
        impl_->action = std::move(action);
        auto &p = *impl_; p.controls.IsPlayEnabled(true); p.controls.IsPauseEnabled(true); p.controls.IsNextEnabled(true); p.controls.IsPreviousEnabled(true);
        p.buttons = p.controls.ButtonPressed([shared = impl_](const auto &, const SystemMediaTransportControlsButtonPressedEventArgs &args) {
            std::string action;
            switch (args.Button()) {
            case SystemMediaTransportControlsButton::Play: action = "play"; break;
            case SystemMediaTransportControlsButton::Pause: action = "pause"; break;
            case SystemMediaTransportControlsButton::Next: action = "next"; break;
            case SystemMediaTransportControlsButton::Previous: action = "previous"; break;
            default: return;
            }
            std::lock_guard lock(shared->mutex); if (shared->action) shared->action({{"action", action}});
        });
        p.seeking = p.controls.PlaybackPositionChangeRequested([shared = impl_](const auto &, const PlaybackPositionChangeRequestedEventArgs &args) {
            std::lock_guard lock(shared->mutex); if (shared->action) shared->action({{"action", "seek"}, {"position", args.RequestedPlaybackPosition().count() / 10000000.0}});
        });
    } catch (const winrt::hresult_error &) { impl_->controls = nullptr; }
}
void SystemMedia::update(const Json &state, const Json &track) {
    auto &p = *impl_; if (!p.controls) return;
    try {
        const auto id = state.value("id", "");
        p.controls.IsEnabled(!id.empty());
        p.controls.PlaybackStatus(id.empty() ? MediaPlaybackStatus::Closed : state.value("playing", false) ? MediaPlaybackStatus::Playing : MediaPlaybackStatus::Paused);
        const auto signature = Json::array({id, track.value("title", ""), track.value("artist", ""), track.value("album", "")}).dump();
        if (signature != p.signature) {
            auto updater = p.controls.DisplayUpdater(); updater.ClearAll();
            if (!id.empty()) {
                updater.Type(MediaPlaybackType::Music); auto music = updater.MusicProperties();
                music.Title(winrt::hstring(wide(track.value("title", "DR Music"))));
                music.Artist(winrt::hstring(wide(track.value("artist", "")))); music.AlbumTitle(winrt::hstring(wide(track.value("album", ""))));
            }
            updater.Update(); p.signature = signature;
        }
        SystemMediaTransportControlsTimelineProperties timeline;
        const auto duration = static_cast<int64_t>(state.value("duration", 0.0) * 10000000);
        const auto position = static_cast<int64_t>(state.value("position", 0.0) * 10000000);
        using winrt::Windows::Foundation::TimeSpan;
        timeline.StartTime(TimeSpan{0}); timeline.MinSeekTime(TimeSpan{0}); timeline.EndTime(TimeSpan{duration}); timeline.MaxSeekTime(TimeSpan{duration}); timeline.Position(TimeSpan{position});
        p.controls.UpdateTimelineProperties(timeline);
    } catch (const winrt::hresult_error &) { /* device/OS controls may disappear independently of playback */ }
}
}
