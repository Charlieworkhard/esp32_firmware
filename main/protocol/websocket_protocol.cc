#include "websocket_protocol.h"

#include <cstring>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>

#define TAG "Websocket"

namespace {

std::string GetDeviceMac() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buffer[18];
    snprintf(buffer, sizeof(buffer), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    return buffer;
}

}  // namespace

WebsocketProtocol::~WebsocketProtocol() {
    Stop();
}

bool WebsocketProtocol::Start(const std::string& url, const std::string& token) {
    if (client_ != nullptr) {
        return true;
    }

    url_ = url;
    token_ = token;

    std::string mac = GetDeviceMac();
    headers_ = "Device-Id: " + mac + "\r\n" + "Client-Id: " + mac + "\r\n" + "Protocol-Version: 1\r\n";
    if (!token_.empty()) {
        headers_ += "Authorization: Bearer " + token_ + "\r\n";
    }

    esp_websocket_client_config_t config = {};
    config.uri = url_.c_str();
    config.headers = headers_.c_str();
    config.buffer_size = 4096;
    config.task_stack = 8192;
    config.network_timeout_ms = 10000;
    config.reconnect_timeout_ms = 3000;
    config.disable_auto_reconnect = false;

    client_ = esp_websocket_client_init(&config);
    if (client_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create websocket client");
        return false;
    }

    esp_websocket_register_events(client_, WEBSOCKET_EVENT_ANY, &WebsocketProtocol::EventHandler,
                                  this);

    ESP_LOGI(TAG, "Connecting to %s", url_.c_str());
    if (esp_websocket_client_start(client_) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start websocket client");
        esp_websocket_client_destroy(client_);
        client_ = nullptr;
        return false;
    }
    return true;
}

void WebsocketProtocol::Stop() {
    if (client_ != nullptr) {
        esp_websocket_client_stop(client_);
        esp_websocket_client_destroy(client_);
        client_ = nullptr;
    }
    connected_ = false;
}

bool WebsocketProtocol::IsConnected() const {
    return client_ != nullptr && connected_;
}

bool WebsocketProtocol::SendJson(cJSON* root) {
    if (client_ == nullptr || !connected_) {
        return false;
    }

    char* text = cJSON_PrintUnformatted(root);
    if (text == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(send_mutex_);
    int ret = esp_websocket_client_send_text(client_, text, strlen(text), pdMS_TO_TICKS(2000));
    cJSON_free(text);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to send text frame");
        return false;
    }
    return true;
}

bool WebsocketProtocol::SendAudio(const uint8_t* data, size_t size, uint32_t timestamp) {
    (void)timestamp;  // 协议版本 1 的二进制帧不带时间戳头
    if (client_ == nullptr || !connected_ || data == nullptr || size == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(send_mutex_);
    int ret = esp_websocket_client_send_bin(client_, reinterpret_cast<const char*>(data), size,
                                            pdMS_TO_TICKS(2000));
    return ret >= 0;
}

void WebsocketProtocol::SendHello() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddStringToObject(root, "transport", "websocket");

    cJSON* features = cJSON_CreateObject();
    cJSON_AddBoolToObject(features, "mcp", false);
    cJSON_AddBoolToObject(features, "aec", false);
    cJSON_AddItemToObject(root, "features", features);

    cJSON* audio = cJSON_CreateObject();
    cJSON_AddStringToObject(audio, "format", "opus");
    cJSON_AddNumberToObject(audio, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio, "channels", 1);
    cJSON_AddNumberToObject(audio, "frame_duration", 60);
    cJSON_AddItemToObject(root, "audio_params", audio);

    bool ok = SendJson(root);
    cJSON_Delete(root);

    if (ok) {
        ESP_LOGI(TAG, "Hello sent");
    } else {
        ESP_LOGE(TAG, "Failed to send hello");
    }
}

void WebsocketProtocol::EventHandler(void* context, esp_event_base_t base, int32_t id,
                                     void* data) {
    auto* self = static_cast<WebsocketProtocol*>(context);
    self->HandleEvent(id, data);
}

void WebsocketProtocol::HandleEvent(int32_t id, void* event_data) {
    auto* data = static_cast<esp_websocket_event_data_t*>(event_data);

    switch (id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected");
            rx_buffer_.clear();
            connected_ = true;
            if (on_connection_) {
                on_connection_(true);
            }
            SendHello();
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected");
            connected_ = false;
            if (on_connection_) {
                on_connection_(false);
            }
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data == nullptr) {
                break;
            }
            if (data->op_code == 0x1 || data->op_code == 0x2) {
                if (data->payload_offset == 0) {
                    rx_buffer_.assign(data->data_ptr, data->data_len);
                } else {
                    rx_buffer_.append(data->data_ptr, data->data_len);
                }
                if (static_cast<int>(rx_buffer_.size()) < data->payload_len) {
                    break;  // 分片未收完
                }
                if (data->op_code == 0x1) {
                    cJSON* root = cJSON_ParseWithLength(rx_buffer_.data(), rx_buffer_.size());
                    if (root != nullptr) {
                        if (on_json_) {
                            on_json_(root);
                        }
                        cJSON_Delete(root);
                    } else {
                        ESP_LOGW(TAG, "Invalid JSON from server");
                    }
                } else if (on_audio_) {
                    auto packet = std::make_unique<AudioPacket>();
                    packet->payload.assign(rx_buffer_.begin(), rx_buffer_.end());
                    on_audio_(std::move(packet));
                }
                rx_buffer_.clear();
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "Websocket error");
            break;

        default:
            break;
    }
}
