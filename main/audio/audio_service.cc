#include "audio_service.h"

#include <algorithm>
#include <cmath>

#include <esp_log.h>

#define TAG "AudioService"

namespace {
constexpr int kFrameSamples = OpusCodec::kFrameSamples;
constexpr int kMaxOpusPacket = 512;
constexpr int kQueueLength = 8;

// 开始录音时补发的历史帧数：8 帧 = 480ms，足够盖住 VAD 的判定延迟。
constexpr size_t kPreRollFrames = 8;
// 播放侧 jitter buffer：攒够 2 帧（120ms）再开播，最多等 120ms。
constexpr UBaseType_t kPlayStartFrames = 2;
constexpr int kPlayStartWaitMs = 120;
// 采集侧 AGC：把语音拉到目标 RMS，太小声时最多放大 6 倍。
constexpr float kAgcTargetRms = 3000.0f;
constexpr float kAgcMaxGain = 6.0f;

// esp_audio_codec 官方说明：编码器所在任务栈约需 40KB，解码器约需 20KB。
// 60ms 帧下 opus_encode 会在栈上开十几 KB 的临时数组，栈给小了必崩。
constexpr int kMicStack = 6144;
constexpr int kEncodeStack = 40960;
constexpr int kPlayStack = 24576;
}  // namespace

AudioService& AudioService::GetInstance() {
    static AudioService instance;
    return instance;
}

bool AudioService::Start() {
    if (running_.exchange(true)) {
        return true;
    }

    if (!AudioIo::GetInstance().Init()) {
        running_ = false;
        return false;
    }
    if (!codec_.Init()) {
        running_ = false;
        return false;
    }

    packet_queue_ = xQueueCreate(kQueueLength, sizeof(std::vector<uint8_t>*));
    pcm_queue_ = xQueueCreate(kQueueLength, sizeof(std::vector<int16_t>*));
    event_queue_ = xQueueCreate(8, sizeof(uint8_t));
    if (packet_queue_ == nullptr || pcm_queue_ == nullptr || event_queue_ == nullptr) {
        running_ = false;
        return false;
    }

    xTaskCreate(
        [](void* arg) {
            static_cast<AudioService*>(arg)->MicTask();
            vTaskDelete(nullptr);
        },
        "audio_mic", kMicStack, this, 8, nullptr);

    xTaskCreate(
        [](void* arg) {
            static_cast<AudioService*>(arg)->EncodeTask();
            vTaskDelete(nullptr);
        },
        "audio_encode", kEncodeStack, this, 5, nullptr);

    xTaskCreate(
        [](void* arg) {
            static_cast<AudioService*>(arg)->PlayTask();
            vTaskDelete(nullptr);
        },
        "audio_play", kPlayStack, this, 6, nullptr);

    ESP_LOGI(TAG, "Audio service started");
    return true;
}

void AudioService::SetCaptureEnabled(bool enabled) {
    capture_enabled_ = enabled;
}

void AudioService::SetMuted(bool muted) {
    muted_ = muted;
}

void AudioService::PushPlaybackPacket(std::vector<uint8_t>&& packet) {
    if (packet_queue_ == nullptr) {
        return;
    }
    auto* item = new std::vector<uint8_t>(std::move(packet));
    if (xQueueSend(packet_queue_, &item, pdMS_TO_TICKS(200)) != pdTRUE) {
        delete item;
        ESP_LOGW(TAG, "Playback queue full, dropping packet");
        return;
    }
    playing_ = true;
}

void AudioService::ClearPlayback() {
    if (packet_queue_ != nullptr) {
        std::vector<uint8_t>* item = nullptr;
        while (xQueueReceive(packet_queue_, &item, 0) == pdTRUE) {
            delete item;
        }
    }
    playing_ = false;
}

void AudioService::PostEvent(Event event) {
    if (event_queue_ == nullptr) {
        return;
    }
    uint8_t value = static_cast<uint8_t>(event);
    xQueueSend(event_queue_, &value, 0);  // 事件队列满时丢弃，不影响音频流
}

bool AudioService::WaitEvent(Event* event, uint32_t timeout_ms) {
    if (event_queue_ == nullptr || event == nullptr) {
        return false;
    }
    uint8_t value = 0;
    if (xQueueReceive(event_queue_, &value, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return false;
    }
    *event = static_cast<Event>(value);
    return true;
}

void AudioService::MicTask() {
    std::vector<int16_t> frame(kFrameSamples);

    while (running_) {
        int read = AudioIo::GetInstance().Read(frame.data(), kFrameSamples);
        if (read <= 0) {
            continue;
        }
        if (muted_) {
            pre_roll_.clear();
            continue;
        }

        // 先拿原始帧做 VAD，再对上行副本做 AGC：噪声底必须建立在真实电平上，
        // 否则 AGC 把安静帧放大后，VAD 会一直被“锁”在说话状态而断不了句。
        int vad = vad_.Process(frame.data(), read);
        if (vad == Vad::kSpeechStart) {
            ESP_LOGI(TAG, "VAD start rms=%.0f thr=%.0f", vad_.LastRms(), vad_.LastThreshold());
            PostEvent(Event::kVadStart);
        } else if (vad == Vad::kSpeechEnd) {
            ESP_LOGI(TAG, "VAD end rms=%.0f thr=%.0f", vad_.LastRms(), vad_.LastThreshold());
            PostEvent(Event::kVadEnd);
        }

        ApplyAgc(frame.data(), read);
        Deliver(frame.data(), read);
    }
}

void AudioService::Deliver(const int16_t* samples, int count) {
    if (!capture_enabled_) {
        // 未开机录音时只留最近几帧，等 VAD 触发后补发。
        pre_roll_.emplace_back(samples, samples + count);
        while (pre_roll_.size() > kPreRollFrames) {
            pre_roll_.pop_front();
        }
        return;
    }

    // 先补发预录帧，剩下的重活（Opus 编码）交给 audio_encode 任务。
    while (!pre_roll_.empty()) {
        auto* item = new std::vector<int16_t>(std::move(pre_roll_.front()));
        pre_roll_.pop_front();
        if (xQueueSend(pcm_queue_, &item, 0) != pdTRUE) {
            delete item;
            break;
        }
    }

    auto* item = new std::vector<int16_t>(samples, samples + count);
    if (xQueueSend(pcm_queue_, &item, 0) != pdTRUE) {
        delete item;  // 编码跟不上时丢帧，保证采集不阻塞
    }
}

void AudioService::ApplyAgc(int16_t* samples, int count) {
    if (count <= 0) {
        return;
    }

    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        sum += static_cast<double>(samples[i]) * samples[i];
    }
    float rms = static_cast<float>(std::sqrt(sum / count));
    if (rms < 1.0f) {
        return;
    }

    // 慢速跟随，避免帧间增益跳变造成爆音。
    float desired = kAgcTargetRms / rms;
    desired = std::clamp(desired, 1.0f, kAgcMaxGain);
    agc_gain_ = agc_gain_ * 0.9f + desired * 0.1f;
    if (agc_gain_ < 1.0f) {
        agc_gain_ = 1.0f;
    }
    if (agc_gain_ <= 1.001f) {
        return;
    }

    for (int i = 0; i < count; ++i) {
        int32_t value = static_cast<int32_t>(samples[i] * agc_gain_);
        samples[i] = static_cast<int16_t>(std::clamp<int32_t>(value, INT16_MIN, INT16_MAX));
    }
}

void AudioService::EncodeTask() {
    std::vector<uint8_t> opus(kMaxOpusPacket);

    while (running_) {
        std::vector<int16_t>* item = nullptr;
        if (xQueueReceive(pcm_queue_, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        int encoded = codec_.Encode(item->data(), opus.data(), opus.size());
        delete item;

        if (encoded > 0 && on_encoded_) {
            on_encoded_(opus.data(), encoded);
        }
    }
}

void AudioService::PlayTask() {
    std::vector<int16_t> pcm(kFrameSamples);
    bool started = false;

    while (running_) {
        // 开播前先攒 kPlayStartFrames 帧，吸收网络抖动，减少句中的静音断点。
        if (!started && uxQueueMessagesWaiting(packet_queue_) > 0) {
            int waited = 0;
            while (uxQueueMessagesWaiting(packet_queue_) < kPlayStartFrames &&
                   waited < kPlayStartWaitMs) {
                vTaskDelay(pdMS_TO_TICKS(10));
                waited += 10;
            }
            started = true;
        }

        std::vector<uint8_t>* item = nullptr;
        if (xQueueReceive(packet_queue_, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
            started = false;
            if (playing_.exchange(false)) {
                PostEvent(Event::kPlaybackFinished);
            }
            continue;
        }

        int decoded = codec_.Decode(item->data(), item->size(), pcm.data(), kFrameSamples);
        delete item;

        if (decoded > 0) {
            AudioIo::GetInstance().Write(pcm.data(), decoded);
        }
    }
}
