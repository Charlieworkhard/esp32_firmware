#ifndef AUDIO_IO_H
#define AUDIO_IO_H

#include <cstdint>
#include <vector>

#include <driver/gpio.h>
#include <driver/i2s_std.h>

// I2S 收发分离：麦克风和功放各占一个 I2S 控制器，互不干扰，采样率也可以不同。
// 麦克风 INMP441：WS/SCK/DIN，只读；功放 MAX98357：BCLK/LRCK/DOUT，只写。
class AudioIo {
public:
    static AudioIo& GetInstance();

    bool Init();
    int Read(int16_t* dst, int samples);
    int Write(const int16_t* src, int samples);

    void SetVolume(int volume);
    int GetVolume() const { return volume_; }

    void EnableTx(bool enable);
    void EnableRx(bool enable);

private:
    AudioIo() = default;

    i2s_chan_handle_t tx_ = nullptr;
    i2s_chan_handle_t rx_ = nullptr;
    // 复用缓冲区，避免每 60ms 帧都做一次堆分配。
    std::vector<int32_t> rx_buffer_;
    std::vector<int32_t> tx_buffer_;
    int volume_ = 80;
    bool tx_enabled_ = false;
    bool rx_enabled_ = false;
};

#endif  // AUDIO_IO_H
