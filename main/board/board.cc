#include "board.h"
#include "config.h"

#include <esp_log.h>
#include <driver/gpio.h>

#define TAG "Board"

namespace {
void ConfigureButton(gpio_num_t pin) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}
}  // namespace

Board& Board::GetInstance() {
    static Board instance;
    return instance;
}

void Board::Init() {
    ESP_LOGI(TAG, "Initializing board");

    ConfigureButton(BOOT_BUTTON_GPIO);
    ConfigureButton(VOLUME_UP_BUTTON_GPIO);
    ConfigureButton(VOLUME_DOWN_BUTTON_GPIO);

    ESP_LOGI(TAG, "Board initialized (boot=%d, vol+ =%d, vol- =%d)", BOOT_BUTTON_GPIO,
             VOLUME_UP_BUTTON_GPIO, VOLUME_DOWN_BUTTON_GPIO);
}

bool Board::IsBootButtonPressed() const {
    return gpio_get_level(BOOT_BUTTON_GPIO) == 0;
}

bool Board::IsVolumeUpPressed() const {
    return gpio_get_level(VOLUME_UP_BUTTON_GPIO) == 0;
}

bool Board::IsVolumeDownPressed() const {
    return gpio_get_level(VOLUME_DOWN_BUTTON_GPIO) == 0;
}
