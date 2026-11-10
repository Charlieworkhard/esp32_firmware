#ifndef SETTINGS_H
#define SETTINGS_H

#include <cstdint>
#include <string>

#include <nvs.h>

// 简单的 NVS 键值存取封装，每个实例对应一个命名空间。
class Settings {
public:
    explicit Settings(const char* ns, bool read_only = false);
    ~Settings();

    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    bool IsValid() const { return handle_ != 0; }

    std::string GetString(const char* key, const std::string& default_value = "");
    int32_t GetInt(const char* key, int32_t default_value = 0);
    bool GetBool(const char* key, bool default_value = false);

    bool SetString(const char* key, const std::string& value);
    bool SetInt(const char* key, int32_t value);
    bool SetBool(const char* key, bool value);

    void Erase();

private:
    nvs_handle_t handle_ = 0;
};

#endif  // SETTINGS_H
