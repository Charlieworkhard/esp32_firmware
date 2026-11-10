#include "audio_io.h"

#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#define TAG "AudioIo"

namespace {
constexpr int kDmaDescNum = 6;
constexpr int kDmaFrameNum = 240;

// 两个控制器共用同一套槽位参数：32 位槽、单声道、取左槽。
i2s_std_config_t MakeStdConfig(int sample_rate) {
    i2s_std_config_t config = {};
    config.clk_cfg.sample_rate_hz = static_cast<uint32_t>(sample_rate);
    config.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    config.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_32BIT;
    config.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    config.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    config.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_32BIT;
    config.slot_cfg.ws_pol = false;
    config.slot_cfg.bit_shift = true;
    config.slot_cfg.left_align = true;
    config.slot_cfg.big_endian = false;
    config.slot_cfg.bit_order_lsb = false;

    config.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    config.gpio_cfg.invert_flags.mclk_inv = false;
    config.gpio_cfg.invert_flags.bclk_inv = false;
    config.gpio_cfg.invert_flags.ws_inv = false;
    return config;
}

i2s_chan_config_t MakeChanConfig(int port) {
    i2s_chan_config_t config = {};
    config.id = port;
    config.role = I2S_ROLE_MASTER;
    config.dma_desc_num = kDmaDescNum;
    config.dma_frame_num = kDmaFrameNum;
    config.auto_clear_after_cb = true;
    config.auto_clear_before_cb = false;
    return config;
}
}  // namespace

AudioIo& AudioIo::GetInstance() {
    static AudioIo instance;
    return instance;
}

bool AudioIo::Init() {
    if (tx_ != nullptr || rx_ != nullptr) {
        return true;
    }

    // 功放通道：只发
    i2s_chan_config_t tx_chan = MakeChanConfig(I2S_NUM_0);
    if (i2s_new_channel(&tx_chan, &tx_, nullptr) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create speaker I2S channel");
        return false;
    }
    i2s_std_config_t tx_config = MakeStdConfig(AUDIO_OUTPUT_SAMPLE_RATE);
    tx_config.gpio_cfg.bclk = AUDIO_I2S_SPK_GPIO_BCLK;
    tx_config.gpio_cfg.ws = AUDIO_I2S_SPK_GPIO_LRCK;
    tx_config.gpio_cfg.dout = AUDIO_I2S_SPK_GPIO_DOUT;
    tx_config.gpio_cfg.din = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(tx_, &tx_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init speaker I2S");
        return false;
    }

    // 麦克风通道：只收
    i2s_chan_config_t rx_chan = MakeChanConfig(I2S_NUM_1);
    if (i2s_new_channel(&rx_chan, nullptr, &rx_) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create microphone I2S channel");
        return false;
    }
    i2s_std_config_t rx_config = MakeStdConfig(AUDIO_INPUT_SAMPLE_RATE);
    rx_config.gpio_cfg.bclk = AUDIO_I2S_MIC_GPIO_SCK;
    rx_config.gpio_cfg.ws = AUDIO_I2S_MIC_GPIO_WS;
    rx_config.gpio_cfg.dout = I2S_GPIO_UNUSED;
    rx_config.gpio_cfg.din = AUDIO_I2S_MIC_GPIO_DIN;
    if (i2s_channel_init_std_mode(rx_, &rx_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init microphone I2S");
        return false;
    }

    EnableTx(true);
    EnableRx(true);
    ESP_LOGI(TAG, "I2S ready: mic %d Hz, speaker %d Hz", AUDIO_INPUT_SAMPLE_RATE,
             AUDIO_OUTPUT_SAMPLE_RATE);
    return true;
}

int AudioIo::Read(int16_t* dst, int samples) {
    if (rx_ == nullptr || !rx_enabled_) {
        return 0;
    }

    if (static_cast<int>(rx_buffer_.size()) < samples) {
        rx_buffer_.resize(samples);
    }
    size_t bytes_read = 0;
    if (i2s_channel_read(rx_, rx_buffer_.data(), samples * sizeof(int32_t), &bytes_read, 200) !=
        ESP_OK) {
        return 0;
    }

    int count = static_cast<int>(bytes_read / sizeof(int32_t));
    for (int i = 0; i < count; ++i) {
        // INMP441 的 24 位数据在 32 位槽的高位，右移 12 位得到 16 位样本。
        int32_t value = rx_buffer_[i] >> 12;
        dst[i] = static_cast<int16_t>(std::clamp<int32_t>(value, INT16_MIN, INT16_MAX));
    }
    return count;
}

int AudioIo::Write(const int16_t* src, int samples) {
    if (tx_ == nullptr || !tx_enabled_) {
        return 0;
    }

    // 音量 0-100 映射到 0-65536 的 16.16 定点增益。
    // int16 样本乘增益后正好落在 int32 的高 16 位（满量程约 2^31），这才是功放能听到的幅度；
    // 如果再右移 16 位，幅度只剩 1/65536，等于静音。
    const int32_t gain = static_cast<int32_t>(std::pow(volume_ / 100.0, 2.0) * 65536.0);
    if (static_cast<int>(tx_buffer_.size()) < samples) {
        tx_buffer_.resize(samples);
    }
    for (int i = 0; i < samples; ++i) {
        int64_t value = static_cast<int64_t>(src[i]) * gain;
        if (value > INT32_MAX) {
            value = INT32_MAX;
        } else if (value < INT32_MIN) {
            value = INT32_MIN;
        }
        tx_buffer_[i] = static_cast<int32_t>(value);
    }

    size_t bytes_written = 0;
    if (i2s_channel_write(tx_, tx_buffer_.data(), samples * sizeof(int32_t), &bytes_written,
                          portMAX_DELAY) != ESP_OK) {
        return 0;
    }
    return static_cast<int>(bytes_written / sizeof(int32_t));
}

void AudioIo::SetVolume(int volume) {
    volume_ = std::clamp(volume, 0, 100);
}

void AudioIo::EnableTx(bool enable) {
    if (tx_ == nullptr || tx_enabled_ == enable) {
        return;
    }
    if (enable) {
        i2s_channel_enable(tx_);
    } else {
        i2s_channel_disable(tx_);
    }
    tx_enabled_ = enable;
}

void AudioIo::EnableRx(bool enable) {
    if (rx_ == nullptr || rx_enabled_ == enable) {
        return;
    }
    if (enable) {
        i2s_channel_enable(rx_);
    } else {
        i2s_channel_disable(rx_);
    }
    rx_enabled_ = enable;
}
