#ifndef WEBSOCKET_PROTOCOL_H
#define WEBSOCKET_PROTOCOL_H

#include "protocol.h"

#include <mutex>
#include <string>

#include <esp_websocket_client.h>

// 文本帧为 JSON，二进制帧为 Opus 数据（协议版本 1）。
class WebsocketProtocol : public Protocol {
public:
    WebsocketProtocol() = default;
    ~WebsocketProtocol() override;

    bool Start(const std::string& url, const std::string& token) override;
    void Stop() override;
    bool IsConnected() const override;

    bool SendAudio(const uint8_t* data, size_t size, uint32_t timestamp) override;
    bool SendJson(cJSON* root) override;

private:
    static void EventHandler(void* context, esp_event_base_t base, int32_t id, void* data);
    void HandleEvent(int32_t id, void* data);

    void SendHello();

    esp_websocket_client_handle_t client_ = nullptr;
    std::string url_;
    std::string headers_;
    std::string token_;
    std::string rx_buffer_;
    std::mutex send_mutex_;
    bool connected_ = false;
};

#endif  // WEBSOCKET_PROTOCOL_H
