#ifndef APPLICATION_H
#define APPLICATION_H

#include <atomic>
#include <cstdint>
#include <memory>

class Protocol;
struct cJSON;

enum DeviceState {
    kDeviceStateStarting,
    kDeviceStateIdle,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateWifiConfiguring,
};

class Application {
public:
    static Application& GetInstance();

    void Start();

    DeviceState GetDeviceState() const { return state_; }
    void SetDeviceState(DeviceState state) { state_ = state; }

private:
    Application() = default;

    bool TryConnect();
    void BeginListening(bool manual);
    void EndListening();
    void HandleButton();
    void HandleVolumeKeys();
    void ApplyVolume(int delta);
    void HandleJson(const cJSON* root);
    void HandleVadChanged(bool speaking);
    void HandlePlaybackFinished();
    void OpenContinueWindow();

    DeviceState state_ = kDeviceStateStarting;
    std::atomic<bool> running_{false};
    std::atomic<bool> manual_listen_{false};
    bool last_button_pressed_ = false;
    bool vol_up_pressed_ = false;
    bool vol_down_pressed_ = false;
    int volume_ = 80;
    int64_t volume_changed_at_ = 0;
    int64_t listen_started_ = 0;
    bool unmute_on_playback_end_ = false;
    int64_t next_connect_attempt_ = 0;
    int64_t speaking_since_ = 0;
    // 仅在这段时间内允许 VAD 自动开麦（免按键续聊），过期后必须按键。
    int64_t continue_until_ = 0;
    // 播放刚结束的缓冲期，避免喇叭余音把自己唤醒。
    int64_t resume_cooldown_until_ = 0;
    std::unique_ptr<Protocol> protocol_;
};

#endif  // APPLICATION_H
