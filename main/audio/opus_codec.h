#ifndef OPUS_CODEC_H
#define OPUS_CODEC_H

#include <cstdint>

// Opus 编解码封装：上行 16kHz，下行 16kHz，帧长 60ms。
class OpusCodec {
public:
    static constexpr int kSampleRate = 16000;
    static constexpr int kFrameDurationMs = 60;
    static constexpr int kFrameSamples = kSampleRate / 1000 * kFrameDurationMs;

    ~OpusCodec();

    bool Init();
    void Close();

    int Encode(const int16_t* pcm, uint8_t* output, int output_capacity);
    int Decode(const uint8_t* data, int size, int16_t* pcm, int pcm_capacity);

private:
    void* encoder_ = nullptr;
    void* decoder_ = nullptr;
    int encode_frame_samples_ = 0;
    int encode_output_size_ = 0;
};

#endif  // OPUS_CODEC_H
