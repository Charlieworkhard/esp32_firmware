#ifndef VAD_H
#define VAD_H

#include <cstdint>

// 基于能量的语音活动检测：自适应噪声底 + 起止帧计数。
// 输入必须是未做 AGC 的原始帧，否则噪声底会被增益带跑。
class Vad {
public:
    enum Result {
        kNone = 0,
        kSpeechStart = 1,
        kSpeechContinue = 2,
        kSpeechEnd = 3,
    };

    int Process(const int16_t* samples, int count);
    void Reset();

    bool IsSpeaking() const { return speaking_; }
    float LastRms() const { return last_rms_; }
    float LastThreshold() const { return last_threshold_; }

private:
    static constexpr int kStartFrames = 2;
    static constexpr int kEndFrames = 16;  // 16 x 60ms = 960ms 静音才断句
    static constexpr float kMinThreshold = 400.0f;
    // 结束阈值比起始阈值低，形成滞回：句中轻微的音量起伏不会提前断句。
    static constexpr float kEndRatio = 0.6f;
    // 兜底：绝对静音也算结束，避免噪声底被抬高后永远断不了句。
    static constexpr float kAbsSilence = 220.0f;
    static constexpr int kAbsSilenceFrames = 8;  // 480ms
    // 兜底：一句话最长 15s，防止麦克风被持续噪声“锁”在说话状态。
    static constexpr int kMaxSpeechFrames = 250;

    float noise_floor_ = 300.0f;
    float last_rms_ = 0.0f;
    float last_threshold_ = 0.0f;
    int above_count_ = 0;
    int below_count_ = 0;
    int abs_below_count_ = 0;
    int speech_frames_ = 0;
    bool speaking_ = false;
};

#endif  // VAD_H
