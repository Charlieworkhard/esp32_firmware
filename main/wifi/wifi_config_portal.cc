#include "wifi_config_portal.h"

#include "wifi_manager.h"

#include <cstring>
#include <string>

#include <esp_http_server.h>
#include <esp_log.h>
#include <sdkconfig.h>

#define TAG "WifiPortal"

namespace {

const char kPageTemplate[] = R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s 配网</title>
<style>
body{font-family:-apple-system,"Segoe UI",sans-serif;background:#f5f6f8;margin:0;padding:24px}
.card{max-width:420px;margin:0 auto;background:#fff;border-radius:14px;padding:24px;box-shadow:0 2px 12px rgba(0,0,0,.08)}
h1{font-size:20px;margin:0 0 6px}
p{color:#666;font-size:14px;margin:0 0 20px}
label{display:block;font-size:13px;color:#444;margin:14px 0 6px}
input{width:100%;box-sizing:border-box;padding:11px 12px;border:1px solid #dcdfe6;border-radius:9px;font-size:15px}
button{width:100%;margin-top:22px;padding:12px;border:0;border-radius:9px;background:#2f6fed;color:#fff;font-size:16px}
.foot{margin-top:16px;font-size:12px;color:#999;text-align:center}
</style>
</head>
<body>
<div class="card">
<h1>%s</h1>
<p>请填写要连接的 Wi-Fi 名称和密码，保存后设备会自动重启。</p>
<form method="post" action="/save">
<label>Wi-Fi 名称</label>
<input name="ssid" maxlength="31" required autocomplete="off">
<label>Wi-Fi 密码</label>
<input name="password" type="password" maxlength="63" autocomplete="off">
<button type="submit">保存并连接</button>
</form>
<div class="foot">仅支持 2.4GHz 网络</div>
</div>
</body>
</html>)HTML";

std::string UrlDecode(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (c == '+') {
            output.push_back(' ');
        } else if (c == '%' && i + 2 < input.size()) {
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                return -1;
            };
            int hi = hex(input[i + 1]);
            int lo = hex(input[i + 2]);
            if (hi >= 0 && lo >= 0) {
                output.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                output.push_back(c);
            }
        } else {
            output.push_back(c);
        }
    }
    return output;
}

bool ExtractField(const std::string& body, const std::string& name, std::string* value) {
    std::string key = name + "=";
    size_t start = 0;
    while (start < body.size()) {
        size_t end = body.find('&', start);
        if (end == std::string::npos) {
            end = body.size();
        }
        std::string pair = body.substr(start, end - start);
        if (pair.compare(0, key.size(), key) == 0) {
            *value = UrlDecode(pair.substr(key.size()));
            return true;
        }
        start = end + 1;
    }
    return false;
}

esp_err_t RootHandler(httpd_req_t* req) {
    std::string page = kPageTemplate;
    // 模板里有两处占位符，统一替换成助手名称。
    const std::string placeholder = "%s";
    size_t pos = 0;
    while ((pos = page.find(placeholder, pos)) != std::string::npos) {
        page.replace(pos, placeholder.size(), CONFIG_APP_ASSISTANT_NAME);
        pos += strlen(CONFIG_APP_ASSISTANT_NAME);
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page.c_str(), page.size());
}

esp_err_t SaveHandler(httpd_req_t* req) {
    char buffer[256] = {};
    int received = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    std::string body(buffer, received);
    std::string ssid;
    std::string password;
    if (!ExtractField(body, "ssid", &ssid) || ssid.empty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        return ESP_FAIL;
    }
    ExtractField(body, "password", &password);

    const char* response =
        "<html><meta charset='utf-8'><body style='font-family:sans-serif;padding:24px'>"
        "<h3>保存成功</h3><p>设备正在重启并连接 Wi-Fi，请稍候。</p></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Received credentials for SSID: %s", ssid.c_str());
    WifiManager::GetInstance().SaveCredentialsAndRestart(ssid, password);
    return ESP_OK;
}

}  // namespace

void WifiConfigPortal::Start() {
    static httpd_handle_t server = nullptr;
    if (server != nullptr) {
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        server = nullptr;
        return;
    }

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = RootHandler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &root);

    httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = SaveHandler,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &save);

    ESP_LOGI(TAG, "Portal HTTP server ready");
}
