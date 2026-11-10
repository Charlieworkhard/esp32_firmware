#ifndef BOARD_H
#define BOARD_H

#include <cstdint>

// Board-level hardware: buttons. I2S 与 OLED 分别在 audio_io / display 里初始化。
class Board {
public:
    static Board& GetInstance();

    void Init();

    bool IsBootButtonPressed() const;
    bool IsVolumeUpPressed() const;
    bool IsVolumeDownPressed() const;

private:
    Board() = default;
};

#endif  // BOARD_H
