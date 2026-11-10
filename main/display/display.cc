#include "display.h"

#include "config.h"
#include "font_glyphs.h"

#include <cstring>

#include <esp_log.h>

#define TAG "Display"

namespace {

constexpr int kColumns = DISPLAY_WIDTH;
constexpr int kPages = DISPLAY_HEIGHT / 8;
constexpr int kAsciiWidth = 8;
constexpr int kHanziWidth = 16;
constexpr int kLineHeight = 16;
constexpr int kI2cAddress = 0x3C;
constexpr int kScrollPause = 12;  // 开始滚动前停留的 tick 数

int DecodeUtf8(const unsigned char* input, uint32_t* codepoint) {
    unsigned char first = input[0];
    if (first < 0x80) {
        *codepoint = first;
        return 1;
    }
    if ((first & 0xE0) == 0xC0) {
        *codepoint = ((first & 0x1F) << 6) | (input[1] & 0x3F);
        return 2;
    }
    if ((first & 0xF0) == 0xE0) {
        *codepoint = ((first & 0x0F) << 12) | ((input[1] & 0x3F) << 6) | (input[2] & 0x3F);
        return 3;
    }
    if ((first & 0xF8) == 0xF0) {
        *codepoint = ((first & 0x07) << 18) | ((input[1] & 0x3F) << 12) |
                     ((input[2] & 0x3F) << 6) | (input[3] & 0x3F);
        return 4;
    }
    *codepoint = 0;
    return 1;
}

int FindHanzi(uint32_t codepoint) {
    int low = 0;
    int high = kHanziCount - 1;
    while (low <= high) {
        int middle = (low + high) / 2;
        uint32_t value = kHanziCodepoints[middle];
        if (value == codepoint) {
            return middle;
        }
        if (value < codepoint) {
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    return -1;
}

}  // namespace

Display& Display::GetInstance() {
    static Display instance;
    return instance;
}

bool Display::Init() {
    if (device_ != nullptr) {
        return true;
    }

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = DISPLAY_SDA_PIN;
    bus_config.scl_io_num = DISPLAY_SCL_PIN;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = 1;

    if (i2c_new_master_bus(&bus_config, &bus_) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus");
        return false;
    }

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = kI2cAddress;
    device_config.scl_speed_hz = 400 * 1000;

    if (i2c_master_bus_add_device(bus_, &device_config, &device_) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add OLED device");
        return false;
    }

    const uint8_t init_commands[] = {
        0xAE,        // display off
        0xD5, 0x80,  // clock divide
        0xA8, 0x1F,  // multiplex 32
        0xD3, 0x00,  // display offset
        0x40,        // start line 0
        0x8D, 0x14,  // charge pump on
        0x20, 0x00,  // horizontal addressing
        0xA1,        // segment remap
        0xC8,        // COM scan direction
        0xDA, 0x02,  // COM pins
        0x81, 0x8F,  // contrast
        0xD9, 0xF1,  // pre-charge
        0xDB, 0x40,  // VCOMH
        0xA4,        // display follows RAM
        0xA6,        // normal (not inverted)
        0x2E,        // deactivate scroll
        0xAF,        // display on
    };
    WriteCommands(init_commands, sizeof(init_commands));

    Clear();
    Flush();
    ESP_LOGI(TAG, "SSD1306 ready (%dx%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    return true;
}

void Display::SetStatus(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_ == text) {
        return;
    }
    status_ = text;
    Refresh();
}

void Display::SetMessage(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (message_ == text) {
        return;
    }
    message_ = text;
    message_width_ = MeasureWidth(text);
    scroll_offset_ = 0;
    scroll_pause_ = kScrollPause;
    Refresh();
}

void Display::Tick() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (device_ == nullptr || message_width_ <= kColumns) {
        return;
    }

    // 停留一小段时间再滚动，避免文字刚出现就动。
    if (scroll_pause_ > 0) {
        scroll_pause_--;
        return;
    }

    scroll_offset_++;
    if (scroll_offset_ > message_width_ + kHanziWidth) {
        scroll_offset_ = 0;
    }
    Refresh();
}

void Display::Refresh() {
    if (device_ == nullptr) {
        return;
    }
    Clear();
    DrawText(0, 0, status_);
    DrawText(-scroll_offset_, kLineHeight, message_);
    Flush();
}

void Display::Clear() {
    memset(framebuffer_, 0, sizeof(framebuffer_));
}

int Display::MeasureWidth(const std::string& text) const {
    int width = 0;
    const auto* cursor = reinterpret_cast<const unsigned char*>(text.c_str());
    while (*cursor != '\0') {
        uint32_t codepoint = 0;
        int length = DecodeUtf8(cursor, &codepoint);
        cursor += length;
        width += (codepoint < 0x80) ? kAsciiWidth : kHanziWidth;
    }
    return width;
}

void Display::DrawText(int x, int y, const std::string& text) {
    const auto* cursor = reinterpret_cast<const unsigned char*>(text.c_str());
    int position = x;

    while (*cursor != '\0') {
        uint32_t codepoint = 0;
        int length = DecodeUtf8(cursor, &codepoint);
        cursor += length;

        if (codepoint < 0x80) {
            DrawAscii(position, y, static_cast<char>(codepoint));
            position += kAsciiWidth;
        } else {
            DrawHanzi(position, y, codepoint);
            position += kHanziWidth;
        }
    }
}

void Display::DrawAscii(int x, int y, char character) {
    int code = static_cast<unsigned char>(character);
    if (code < 32 || code > 126) {
        return;
    }
    const uint8_t* glyph = kAsciiFont[code - 32];
    int page = y / 8;
    if (page + 1 >= kPages) {
        return;
    }
    for (int i = 0; i < kAsciiWidth; ++i) {
        int column = x + i;
        if (column < 0 || column >= kColumns) {
            continue;
        }
        framebuffer_[page][column] = glyph[i * 2];
        framebuffer_[page + 1][column] = glyph[i * 2 + 1];
    }
}

void Display::DrawHanzi(int x, int y, uint32_t codepoint) {
    int index = FindHanzi(codepoint);
    if (index < 0) {
        return;  // 字库中没有该字
    }

    const uint8_t* glyph = kHanziFont[index];
    int page = y / 8;
    if (page + 1 >= kPages) {
        return;
    }
    for (int i = 0; i < kHanziWidth; ++i) {
        int column = x + i;
        if (column < 0 || column >= kColumns) {
            continue;
        }
        framebuffer_[page][column] = glyph[i * 2];
        framebuffer_[page + 1][column] = glyph[i * 2 + 1];
    }
}

void Display::Flush() {
    for (int page = 0; page < kPages; ++page) {
        const uint8_t commands[] = {
            static_cast<uint8_t>(0xB0 | page),
            0x00,
            0x10,
        };
        WriteCommands(commands, sizeof(commands));
        WriteData(framebuffer_[page], kColumns);
    }
}

void Display::WriteCommands(const uint8_t* commands, int count) {
    uint8_t buffer[32];
    if (count > static_cast<int>(sizeof(buffer)) - 1) {
        return;
    }
    buffer[0] = 0x00;  // 控制字节：命令
    memcpy(buffer + 1, commands, count);
    i2c_master_transmit(device_, buffer, count + 1, 100);
}

void Display::WriteData(const uint8_t* data, int count) {
    uint8_t buffer[1 + 128];
    if (count > 128) {
        return;
    }
    buffer[0] = 0x40;  // 控制字节：数据
    memcpy(buffer + 1, data, count);
    i2c_master_transmit(device_, buffer, count + 1, 100);
}
