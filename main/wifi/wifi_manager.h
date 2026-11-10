#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <atomic>
#include <string>

#include <esp_event.h>
#include <esp_netif.h>

// 管理 STA 连接：优先使用 NVS 中保存的账号，失败时进入 AP 配网模式。
class WifiManager {
public:
    static WifiManager& GetInstance();

    void Start();
    void Connect(const std::string& ssid, const std::string& password);
    void StartConfigPortal();

    bool IsConnected() const { return connected_.load(); }
    bool IsPortalActive() const { return portal_active_.load(); }
    std::string GetIpAddress() const;
    std::string GetSsid() const;

    // 保存账号并重启，供配网页面调用。
    void SaveCredentialsAndRestart(const std::string& ssid, const std::string& password);

private:
    WifiManager() = default;

    static void EventHandler(void* arg, esp_event_base_t base, int32_t id, void* data);
    void HandleWifiEvent(int32_t id);
    void HandleGotIp();
    void ScheduleConnectTimeout();

    esp_netif_t* sta_netif_ = nullptr;
    esp_netif_t* ap_netif_ = nullptr;

    std::atomic<bool> connected_{false};
    std::atomic<bool> portal_active_{false};
    std::atomic<bool> started_{false};

    std::string ssid_;
    std::string ip_address_;
};

#endif  // WIFI_MANAGER_H
