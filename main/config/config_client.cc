#include "config_client.h"

#include <cstring>
#include <string>

#include <cJSON.h>
#include <esp_http_client.h>
#include <esp_log.h>

#define TAG "ConfigClient"

namespace {

constexpr int kBufferSize = 1024;

struct ResponseBuffer {
    std::string data;
};

esp_err_t HttpEventHandler(esp_http_client_event_t* event) {
    switch (event->event_id) {
        case HTTP_EVENT_ON_DATA: {
            auto* buffer = static_cast<ResponseBuffer*>(event->user_data);
            if (buffer != nullptr && event->data_len > 0) {
                buffer->data.append(static_cast<const char*>(event->data), event->data_len);
            }
            break;
        }
        default:
            break;
    }
    return ESP_OK;
}

}  // namespace

ServerConfig ConfigClient::Fetch(const std::string& url) {
    ServerConfig result;

    ResponseBuffer buffer;
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 8000;
    config.event_handler = HttpEventHandler;
    config.user_data = &buffer;
    config.buffer_size = kBufferSize;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return result;
    }

    // 服务端的配置接口挂在 POST 上（GET 只是健康检查），body 带上固件版本。
    const char* body = "{\"application\":{\"version\":\"0.1.0\"}}";
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "Config request failed: err=%s, status=%d", esp_err_to_name(err), status);
        return result;
    }

    cJSON* root = cJSON_Parse(buffer.data.c_str());
    if (root == nullptr) {
        ESP_LOGE(TAG, "Invalid config JSON");
        return result;
    }

    const cJSON* websocket = cJSON_GetObjectItemCaseSensitive(root, "websocket");
    if (cJSON_IsObject(websocket)) {
        const cJSON* url_item = cJSON_GetObjectItemCaseSensitive(websocket, "url");
        if (cJSON_IsString(url_item) && url_item->valuestring != nullptr) {
            result.websocket_url = url_item->valuestring;
        }
        const cJSON* token_item = cJSON_GetObjectItemCaseSensitive(websocket, "token");
        if (cJSON_IsString(token_item) && token_item->valuestring != nullptr) {
            result.token = token_item->valuestring;
        }
        const cJSON* version_item = cJSON_GetObjectItemCaseSensitive(websocket, "version");
        if (cJSON_IsNumber(version_item)) {
            result.protocol_version = version_item->valueint;
        }
    }

    cJSON_Delete(root);

    result.valid = !result.websocket_url.empty();
    if (result.valid) {
        ESP_LOGI(TAG, "Server config: %s (protocol v%d)", result.websocket_url.c_str(),
                 result.protocol_version);
    } else {
        ESP_LOGE(TAG, "Config missing websocket.url");
    }
    return result;
}
