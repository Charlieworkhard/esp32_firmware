#ifndef CONFIG_H
#define CONFIG_H

#include <driver/gpio.h>

// ---- 音频采样率 ----
// 麦克风与功放各用一个 I2S 控制器，采样率可以不同；这里都和服务器对齐到 16kHz。
#define AUDIO_INPUT_SAMPLE_RATE 16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000

// ---- I2S：麦克风 INMP441（独立控制器，只收） ----
#define AUDIO_I2S_MIC_GPIO_WS GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN GPIO_NUM_6

// ---- I2S：功放 MAX98357（独立控制器，只发） ----
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

// ---- Buttons / LED ----
#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define TOUCH_BUTTON_GPIO GPIO_NUM_47
#define VOLUME_UP_BUTTON_GPIO GPIO_NUM_40
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39
#define BUILTIN_LED_GPIO GPIO_NUM_48

// ---- OLED (SSD1306 over I2C) ----
#define DISPLAY_SDA_PIN GPIO_NUM_41
#define DISPLAY_SCL_PIN GPIO_NUM_42
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 32
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true

#endif  // CONFIG_H
