#ifndef AUDIO_SERVICE_H
#define AUDIO_SERVICE_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "audio_io.h"
#include "opus_codec.h"
#include "vad.h"

// 音频流水线：麦克风采集 -> VAD -> PCM 队列；
// PCM 队列 -> Opus 编码 -> 上行回调。
// 下行 Opus -> 解码 -> I2S 播放。
class AudioService {
public:
    // 音频任务产生、应用主循环消费的事件。
    enum class Event : uint8_t {
        kVadStart,
        kVadEnd,
        kPlaybackFinished,
    };

    static AudioService& GetInstance();

    bool Start();

    void SetCaptureEnabled(bool enabled);
    void SetMuted(bool muted);
    void ResetVad() { vad_.Reset(); }

    void PushPlaybackPacket(std::vector<uint8_t>&& packet);
    void ClearPlayback();
    bool IsPlaying() const { return playing_.load(); }

    void SetVolume(int volume) { AudioIo::GetInstance().SetVolume(volume); }

    void OnEncoded(std::function<void(const uint8_t*, size_t)> callback) {
        on_encoded_ = std::move(callback);
    }

    // 最多等待 timeout_ms 取一个事件，超时返回 false。
    bool WaitEvent(Event* event, uint32_t timeout_ms);

private:
    AudioService() = default;

    void MicTask();
    void EncodeTask();
    void PlayTask();
    void PostEvent(Event event);
    void ApplyAgc(int16_t* samples, int count);
    void Deliver(const int16_t* samples, int count);

    OpusCodec codec_;
    Vad vad_;

    std::atomic<bool> capture_enabled_{false};
    std::atomic<bool> muted_{false};
    std::atomic<bool> playing_{false};
    std::atomic<bool> running_{false};

    QueueHandle_t pcm_queue_ = nullptr;
    QueueHandle_t packet_queue_ = nullptr;
    QueueHandle_t event_queue_ = nullptr;

    // VAD 触发前的若干帧，开始录音时补发，避免吃掉第一个音节。
    std::deque<std::vector<int16_t>> pre_roll_;
    float agc_gain_ = 1.0f;

    std::function<void(const uint8_t*, size_t)> on_encoded_;
};

#endif  // AUDIO_SERVICE_H
