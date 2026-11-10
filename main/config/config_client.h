#ifndef CONFIG_CLIENT_H
#define CONFIG_CLIENT_H

#include <string>

struct ServerConfig {
    std::string websocket_url;
    std::string token;
    int protocol_version = 1;
    bool valid = false;
};

// 启动时向固定地址请求配置，拿到 WebSocket 地址。
class ConfigClient {
public:
    static ServerConfig Fetch(const std::string& url);
};

#endif  // CONFIG_CLIENT_H
