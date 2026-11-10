#include "application.h"

#include "audio/audio_service.h"
#include "board.h"
#include "config/config_client.h"
#include "display/display.h"
#include "protocol/websocket_protocol.h"
#include "sdkconfig.h"
#include "system/settings.h"
#include "wifi/wifi_manager.h"

#include <algorithm>
#include <cJSON.h>
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>

#define TAG "Application"

namespace {
// 回复结束后 3 秒内允许免按键接着聊，过期后必须按键开麦。
constexpr int64_t kContinueWindowUs = 3LL * 1000 * 1000;
// 播放刚结束时的余音缓冲期，期间忽略 VAD。
constexpr int64_t kResumeCooldownUs = 600LL * 1000;
}  // namespace

Application& Application::GetInstance() {
    static Application instance;
    return instance;
}

void Application::Start() {
    ESP_LOGI(TAG, "Starting application, assistant: %s", CONFIG_APP_ASSISTANT_NAME);
    ESP_LOGI(TAG, "Firmware build %s %s", __DATE__, __TIME__);

    Board::GetInstance().Init();
    Display::GetInstance().Init();
    Display::GetInstance().SetStatus("启动中");
    Display::GetInstance().SetMessage("我是" CONFIG_APP_ASSISTANT_NAME);

    AudioService& audio = AudioService::GetInstance();
    Settings settings("app");
    volume_ = settings.GetInt("volume", 80);
    if (volume_ < 10 || volume_ > 100) {
        volume_ = 80;
    }
    audio.SetVolume(volume_);
    ESP_LOGI(TAG, "Volume %d", volume_);

    audio.OnEncoded([this](const uint8_t* data, size_t size) {
        if (protocol_ != nullptr) {
            protocol_->SendAudio(data, size, 0);
        }
    });
    audio.Start();

    WifiManager::GetInstance().Start();
    if (WifiManager::GetInstance().IsPortalActive()) {
        state_ = kDeviceStateWifiConfiguring;
        Display::GetInstance().SetStatus("配网模式");
        Display::GetInstance().SetMessage("热点已开启");
        ESP_LOGI(TAG, "Waiting for Wi-Fi configuration");
    }

    running_ = true;
    while (running_) {
        if (state_ == kDeviceStateWifiConfiguring) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // 音频任务只投递事件，状态切换统一在主任务里做，避免占音频任务的栈。
        AudioService::Event event;
        if (AudioService::GetInstance().WaitEvent(&event, 50)) {
            if (event == AudioService::Event::kPlaybackFinished) {
                HandlePlaybackFinished();
            } else {
                HandleVadChanged(event == AudioService::Event::kVadStart);
            }
        }

        HandleButton();
        HandleVolumeKeys();
        Display::GetInstance().Tick();

        // 兜底：聆听超过 20s 还没被 VAD 断句，强制结束，避免麦克风一直开着。
        if (state_ == kDeviceStateListening && listen_started_ > 0 &&
            esp_timer_get_time() - listen_started_ > 20 * 1000 * 1000) {
            ESP_LOGW(TAG, "Listening timeout, stop");
            EndListening();
        }

        // 兜底：等回复超时（服务器没回 tts start/stop），恢复待命状态。
        // 空识别时后端不会回任何消息，不能让设备一直卡在“思考中”闭麦。
        if (state_ == kDeviceStateSpeaking && !AudioService::GetInstance().IsPlaying() &&
            esp_timer_get_time() - speaking_since_ > 10 * 1000 * 1000) {
            ESP_LOGW(TAG, "Reply timeout, back to idle");
            AudioService& audio = AudioService::GetInstance();
            audio.SetMuted(false);
            audio.ResetVad();
            unmute_on_playback_end_ = false;
            state_ = kDeviceStateIdle;
            Display::GetInstance().SetStatus("待命");
            OpenContinueWindow();
        }

        if (state_ == kDeviceStateStarting && WifiManager::GetInstance().IsConnected() &&
            esp_timer_get_time() >= next_connect_attempt_) {
            ESP_LOGI(TAG, "Network ready at %s",
                     WifiManager::GetInstance().GetIpAddress().c_str());
            Display::GetInstance().SetStatus("连接中");
            Display::GetInstance().SetMessage(WifiManager::GetInstance().GetIpAddress());
            if (TryConnect()) {
                state_ = kDeviceStateIdle;
            } else {
                // 失败后 5s 再试，期间保持 Starting，按键与滚动照常。
                next_connect_attempt_ = esp_timer_get_time() + 5 * 1000 * 1000;
            }
        }
    }
}

bool Application::TryConnect() {
    if (protocol_ == nullptr) {
        auto websocket = std::make_unique<WebsocketProtocol>();
        websocket->OnConnectionChanged([this](bool connected) {
            ESP_LOGI(TAG, "Server connection %s", connected ? "up" : "down");
            if (!connected) {
                state_ = kDeviceStateStarting;
                AudioService& audio = AudioService::GetInstance();
                audio.SetCaptureEnabled(false);
                audio.ClearPlayback();
                unmute_on_playback_end_ = false;
            }
        });
        websocket->OnIncomingJson([this](const cJSON* root) { HandleJson(root); });
        websocket->OnIncomingAudio([](std::unique_ptr<AudioPacket> packet) {
            AudioService::GetInstance().PushPlaybackPacket(std::move(packet->payload));
        });
        protocol_ = std::move(websocket);
    }

    ServerConfig config = ConfigClient::Fetch(CONFIG_APP_CONFIG_URL);
    if (!config.valid) {
        ESP_LOGE(TAG, "Failed to fetch server config, retry in 5s");
        return false;
    }

    return protocol_->Start(config.websocket_url, config.token);
}

void Application::HandleJson(const cJSON* root) {
    const cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type)) {
        return;
    }

    if (strcmp(type->valuestring, "tts") == 0) {
        const cJSON* state = cJSON_GetObjectItemCaseSensitive(root, "state");
        if (!cJSON_IsString(state)) {
            return;
        }
        if (strcmp(state->valuestring, "start") == 0) {
            AudioService::GetInstance().SetMuted(true);
            state_ = kDeviceStateSpeaking;
            speaking_since_ = esp_timer_get_time();
            Display::GetInstance().SetStatus("说话中");
        } else if (strcmp(state->valuestring, "stop") == 0) {
            AudioService& audio = AudioService::GetInstance();
            if (audio.IsPlaying()) {
                // 队列里还有音频，等播完再解除静音并开窗。
                unmute_on_playback_end_ = true;
            } else {
                audio.SetMuted(false);
                audio.ResetVad();
            }
            state_ = kDeviceStateIdle;
            Display::GetInstance().SetStatus("待命");
            if (!audio.IsPlaying()) {
                OpenContinueWindow();
            }
        }
    } else if (strcmp(type->valuestring, "stt") == 0) {
        const cJSON* text = cJSON_GetObjectItemCaseSensitive(root, "text");
        if (cJSON_IsString(text)) {
            ESP_LOGI(TAG, "Recognized: %s", text->valuestring);
            Display::GetInstance().SetMessage(text->valuestring);
        }
    } else if (strcmp(type->valuestring, "llm") == 0) {
        const cJSON* emotion = cJSON_GetObjectItemCaseSensitive(root, "emotion");
        const cJSON* text = cJSON_GetObjectItemCaseSensitive(root, "text");
        ESP_LOGI(TAG, "Reply: %s (%s)", cJSON_IsString(text) ? text->valuestring : "",
                 cJSON_IsString(emotion) ? emotion->valuestring : "");
        if (cJSON_IsString(text)) {
            Display::GetInstance().SetMessage(text->valuestring);
        }
    } else if (strcmp(type->valuestring, "hello") == 0) {
        ESP_LOGI(TAG, "Handshake complete");
        Display::GetInstance().SetStatus("待命");
    }
}

void Application::HandleVadChanged(bool speaking) {
    if (speaking) {
        // 自动开麦只允许发生在回复结束后的续聊窗口内，其余时间必须按键。
        if (state_ != kDeviceStateIdle) {
            return;
        }
        int64_t now = esp_timer_get_time();
        if (now > continue_until_ || now < resume_cooldown_until_) {
            return;
        }
        ESP_LOGI(TAG, "Continue window hit, listen again");
        BeginListening(false);
        return;
    }

    // 静音够久就结束这一轮，manual 启动的也一样，避免必须再按一次。
    if (state_ == kDeviceStateListening) {
        EndListening();
    }
}

void Application::OpenContinueWindow() {
    int64_t now = esp_timer_get_time();
    continue_until_ = now + kContinueWindowUs;
    resume_cooldown_until_ = now + kResumeCooldownUs;
}

void Application::HandlePlaybackFinished() {
    AudioService& audio = AudioService::GetInstance();
    // 队列播完再解除静音，避免喇叭余音被麦克风拾到。
    if (unmute_on_playback_end_) {
        unmute_on_playback_end_ = false;
        audio.SetMuted(false);
        audio.ResetVad();
    }
    // 真正播完的时刻才开续聊窗口，长回复不会把窗口耗在播放上。
    if (state_ == kDeviceStateIdle) {
        OpenContinueWindow();
    }
}

void Application::BeginListening(bool manual) {
    if (protocol_ == nullptr || !protocol_->IsConnected()) {
        return;
    }

    manual_listen_ = manual;
    AudioService& audio = AudioService::GetInstance();
    audio.ClearPlayback();
    audio.ResetVad();
    audio.SetMuted(false);
    audio.SetCaptureEnabled(true);

    protocol_->SendStartListening(manual ? "manual" : "auto");
    state_ = kDeviceStateListening;
    listen_started_ = esp_timer_get_time();
    Display::GetInstance().SetStatus("聆听中");
    Display::GetInstance().SetMessage("");
    ESP_LOGI(TAG, "Listening (%s)", manual ? "manual" : "auto");
}

void Application::EndListening() {
    AudioService& audio = AudioService::GetInstance();
    audio.SetCaptureEnabled(false);
    audio.SetMuted(true);

    if (protocol_ != nullptr) {
        protocol_->SendStopListening();
    }

    // 明确结束这一轮，不再留自动续聊的口子。
    continue_until_ = 0;
    resume_cooldown_until_ = 0;
    listen_started_ = 0;
    state_ = kDeviceStateSpeaking;
    manual_listen_ = false;
    speaking_since_ = esp_timer_get_time();
    Display::GetInstance().SetStatus("思考中");
    ESP_LOGI(TAG, "Listening stopped");
}

void Application::HandleVolumeKeys() {
    bool up = Board::GetInstance().IsVolumeUpPressed();
    bool down = Board::GetInstance().IsVolumeDownPressed();
    int64_t now = esp_timer_get_time();

    // 按下沿触发，15ms 采样周期足够；加 150ms 冷却防抖。
    if (up && !vol_up_pressed_ && now - volume_changed_at_ > 150 * 1000) {
        ApplyVolume(+10);
    } else if (down && !vol_down_pressed_ && now - volume_changed_at_ > 150 * 1000) {
        ApplyVolume(-10);
    }
    vol_up_pressed_ = up;
    vol_down_pressed_ = down;
}

void Application::ApplyVolume(int delta) {
    int next = std::clamp(volume_ + delta, 10, 100);
    if (next == volume_) {
        return;
    }
    volume_ = next;
    volume_changed_at_ = esp_timer_get_time();
    AudioService::GetInstance().SetVolume(volume_);

    Settings settings("app");
    settings.SetInt("volume", volume_);

    Display::GetInstance().SetMessage("音量 " + std::to_string(volume_));
    ESP_LOGI(TAG, "Volume -> %d", volume_);
}

void Application::HandleButton() {
    bool pressed = Board::GetInstance().IsBootButtonPressed();
    if (pressed == last_button_pressed_) {
        return;
    }
    last_button_pressed_ = pressed;
    if (!pressed) {
        return;  // 只在按下时触发
    }

    AudioService& audio = AudioService::GetInstance();

    // 按键是开麦/关麦的开关，任何状态下按下都优先“关”，不会被动开麦。
    if (state_ == kDeviceStateListening) {
        EndListening();
        return;
    }

    if (state_ == kDeviceStateSpeaking) {
        // 打断回复并彻底关麦，停在待命。
        if (protocol_ != nullptr) {
            protocol_->SendAbortSpeaking();
        }
        audio.ClearPlayback();
        audio.SetCaptureEnabled(false);
        audio.SetMuted(true);
        audio.ResetVad();
        unmute_on_playback_end_ = false;
        continue_until_ = 0;
        resume_cooldown_until_ = 0;
        state_ = kDeviceStateIdle;
        Display::GetInstance().SetStatus("待命");
        ESP_LOGI(TAG, "Aborted by button, microphone closed");
        return;
    }

    if (state_ == kDeviceStateIdle) {
        continue_until_ = 0;  // 手动开麦后不再走自动续聊逻辑
        BeginListening(true);
    }
}
