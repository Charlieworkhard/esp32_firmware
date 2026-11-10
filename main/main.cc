#include <cstdio>

#include "application.h"
#include "sdkconfig.h"

#include <esp_log.h>
#include <nvs_flash.h>

#define TAG "Main"

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Firmware %s booting", "0.1.0");
    Application::GetInstance().Start();
}
