#ifndef DISPLAY_H
#define DISPLAY_H

#include <cstdint>
#include <mutex>
#include <string>

#include <driver/i2c_master.h>

// SSD1306 128x32 单色 OLED：第一行状态，第二行消息（超长自动横向滚动）。
class Display {
public:
    static Display& GetInstance();

    bool Init();
    void SetStatus(const std::string& text);
    void SetMessage(const std::string& text);

    // 由主循环周期调用（约 50ms），驱动滚动。
    void Tick();

private:
    Display() = default;

    void Refresh();
    void Clear();
    void DrawText(int x, int y, const std::string& text);
    void DrawAscii(int x, int y, char character);
    void DrawHanzi(int x, int y, uint32_t codepoint);
    int MeasureWidth(const std::string& text) const;
    void Flush();
    void WriteCommands(const uint8_t* commands, int count);
    void WriteData(const uint8_t* data, int count);

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t device_ = nullptr;
    // 主任务（Tick）和 WebSocket 回调都会写屏，串行化避免 I2C 事务交叉。
    std::mutex mutex_;
    uint8_t framebuffer_[4][128] = {};
    std::string status_;
    std::string message_;
    int message_width_ = 0;
    int scroll_offset_ = 0;
    int scroll_pause_ = 0;
};

#endif  // DISPLAY_H
