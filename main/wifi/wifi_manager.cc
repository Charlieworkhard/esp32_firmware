#include "wifi_manager.h"

#include "settings.h"
#include "wifi_config_portal.h"

#include <cstring>

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#define TAG "WifiManager"

namespace {
constexpr int kConnectTimeoutMs = 15000;
constexpr const char* kNvsNamespace = "wifi";

std::string MakeApSsid() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[40];
    snprintf(ssid, sizeof(ssid), "%s-%02X%02X", CONFIG_APP_AP_SSID_PREFIX, mac[4], mac[5]);
    return ssid;
}
}  // namespace

WifiManager& WifiManager::GetInstance() {
    static WifiManager instance;
    return instance;
}

void WifiManager::Start() {
    if (started_.exchange(true)) {
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    sta_netif_ = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiManager::EventHandler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiManager::EventHandler, this, nullptr));

    Settings settings(kNvsNamespace, true);
    ssid_ = settings.GetString("ssid");
    std::string password = settings.GetString("password");

    if (ssid_.empty()) {
        ESP_LOGI(TAG, "No saved Wi-Fi credentials, starting config portal");
        StartConfigPortal();
        return;
    }

    ESP_LOGI(TAG, "Connecting to saved network: %s", ssid_.c_str());
    Connect(ssid_, password);
}

void WifiManager::Connect(const std::string& ssid, const std::string& password) {
    ssid_ = ssid;

    wifi_config_t config = {};
    strncpy(reinterpret_cast<char*>(config.sta.ssid), ssid.c_str(), sizeof(config.sta.ssid) - 1);
    strncpy(reinterpret_cast<char*>(config.sta.password), password.c_str(),
            sizeof(config.sta.password) - 1);
    config.sta.threshold.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    // 关掉 modem sleep：否则 DTIM 休眠会让下行音频出现周期性卡顿和延迟。
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_ERROR_CHECK(esp_wifi_connect());

    ScheduleConnectTimeout();
}

void WifiManager::ScheduleConnectTimeout() {
    xTaskCreate(
        [](void* arg) {
            vTaskDelay(pdMS_TO_TICKS(kConnectTimeoutMs));
            auto* self = static_cast<WifiManager*>(arg);
            if (!self->connected_ && !self->portal_active_) {
                ESP_LOGW(TAG, "Connect timeout, switching to config portal");
                self->StartConfigPortal();
            }
            vTaskDelete(nullptr);
        },
        "wifi_timeout", 3072, this, 5, nullptr);
}

void WifiManager::StartConfigPortal() {
    if (portal_active_.exchange(true)) {
        return;
    }

    connected_ = false;
    esp_wifi_disconnect();  // 未启动时返回错误，忽略即可

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    // AP 的 netif 必须在 esp_wifi_start() 之前创建，否则拿不到 192.168.4.1。
    if (ap_netif_ == nullptr) {
        ap_netif_ = esp_netif_create_default_wifi_ap();
    }

    wifi_config_t ap_config = {};
    std::string ap_ssid = MakeApSsid();
    strncpy(reinterpret_cast<char*>(ap_config.ap.ssid), ap_ssid.c_str(),
            sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = ap_ssid.size();
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 2;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Config portal started, SSID: %s, open http://192.168.4.1", ap_ssid.c_str());
    WifiConfigPortal::Start();
}

void WifiManager::SaveCredentialsAndRestart(const std::string& ssid, const std::string& password) {
    Settings settings(kNvsNamespace);

    // 切换网络前先清掉旧记录，避免残留。
    settings.Erase();
    settings.SetString("ssid", ssid);
    settings.SetString("password", password);

    ESP_LOGI(TAG, "Credentials saved for %s, restarting", ssid.c_str());
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

std::string WifiManager::GetIpAddress() const {
    return ip_address_;
}

std::string WifiManager::GetSsid() const {
    return ssid_;
}

void WifiManager::EventHandler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    auto* self = static_cast<WifiManager*>(arg);
    if (base == WIFI_EVENT) {
        self->HandleWifiEvent(id);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        self->HandleGotIp();
    }
}

void WifiManager::HandleWifiEvent(int32_t id) {
    if (id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Station started");
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        connected_ = false;
        if (!portal_active_) {
            ESP_LOGW(TAG, "Disconnected, retrying in 2s");
            // 不要阻塞事件循环，交给独立任务延时重连。
            xTaskCreate(
                [](void*) {
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    esp_wifi_connect();
                    vTaskDelete(nullptr);
                },
                "wifi_retry", 2048, nullptr, 5, nullptr);
        }
    }
}

void WifiManager::HandleGotIp() {
    esp_netif_ip_info_t ip_info = {};
    if (sta_netif_ != nullptr && esp_netif_get_ip_info(sta_netif_, &ip_info) == ESP_OK) {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), IPSTR, IP2STR(&ip_info.ip));
        ip_address_ = buffer;
    }
    connected_ = true;
    ESP_LOGI(TAG, "Connected, IP: %s", ip_address_.c_str());
}
