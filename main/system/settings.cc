#include "settings.h"

#include <esp_log.h>

#define TAG "Settings"

Settings::Settings(const char* ns, bool read_only) {
    esp_err_t err = nvs_open(ns, read_only ? NVS_READONLY : NVS_READWRITE, &handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open namespace %s: %s", ns, esp_err_to_name(err));
        handle_ = 0;
    }
}

Settings::~Settings() {
    if (handle_ != 0) {
        nvs_close(handle_);
    }
}

std::string Settings::GetString(const char* key, const std::string& default_value) {
    if (handle_ == 0) {
        return default_value;
    }
    size_t length = 0;
    if (nvs_get_str(handle_, key, nullptr, &length) != ESP_OK || length == 0) {
        return default_value;
    }
    std::string value(length, '\0');
    if (nvs_get_str(handle_, key, value.data(), &length) != ESP_OK) {
        return default_value;
    }
    value.resize(length > 0 ? length - 1 : 0);
    return value;
}

int32_t Settings::GetInt(const char* key, int32_t default_value) {
    if (handle_ == 0) {
        return default_value;
    }
    int32_t value = default_value;
    if (nvs_get_i32(handle_, key, &value) != ESP_OK) {
        return default_value;
    }
    return value;
}

bool Settings::GetBool(const char* key, bool default_value) {
    if (handle_ == 0) {
        return default_value;
    }
    uint8_t value = default_value ? 1 : 0;
    if (nvs_get_u8(handle_, key, &value) != ESP_OK) {
        return default_value;
    }
    return value != 0;
}

bool Settings::SetString(const char* key, const std::string& value) {
    if (handle_ == 0) {
        return false;
    }
    esp_err_t err = nvs_set_str(handle_, key, value.c_str());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write %s: %s", key, esp_err_to_name(err));
        return false;
    }
    return nvs_commit(handle_) == ESP_OK;
}

bool Settings::SetInt(const char* key, int32_t value) {
    if (handle_ == 0) {
        return false;
    }
    return nvs_set_i32(handle_, key, value) == ESP_OK && nvs_commit(handle_) == ESP_OK;
}

bool Settings::SetBool(const char* key, bool value) {
    if (handle_ == 0) {
        return false;
    }
    return nvs_set_u8(handle_, key, value ? 1 : 0) == ESP_OK && nvs_commit(handle_) == ESP_OK;
}

void Settings::Erase() {
    if (handle_ != 0) {
        nvs_erase_all(handle_);
        nvs_commit(handle_);
    }
}
