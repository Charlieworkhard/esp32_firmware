#include "vad.h"

#include <cmath>

void Vad::Reset() {
    noise_floor_ = 300.0f;
    last_rms_ = 0.0f;
    last_threshold_ = 0.0f;
    above_count_ = 0;
    below_count_ = 0;
    abs_below_count_ = 0;
    speech_frames_ = 0;
    speaking_ = false;
}

int Vad::Process(const int16_t* samples, int count) {
    if (count <= 0) {
        return kNone;
    }

    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        sum += static_cast<double>(samples[i]) * samples[i];
    }
    float rms = static_cast<float>(std::sqrt(sum / count));

    const float threshold = std::fmax(kMinThreshold, noise_floor_ * 3.0f);
    last_rms_ = rms;
    last_threshold_ = threshold;

    if (!speaking_ && rms < threshold) {
        noise_floor_ = noise_floor_ * 0.98f + rms * 0.02f;
    }

    if (rms > threshold) {
        above_count_++;
        below_count_ = 0;
        abs_below_count_ = 0;
    } else {
        above_count_ = 0;
        // 只有明显低于阈值才算“静音”；介于两者之间的帧只拖住计数，不累加也不清零。
        if (rms < threshold * kEndRatio) {
            below_count_++;
        }
        if (rms < kAbsSilence) {
            abs_below_count_++;
        } else {
            abs_below_count_ = 0;
        }
    }

    if (!speaking_) {
        if (above_count_ >= kStartFrames) {
            speaking_ = true;
            below_count_ = 0;
            abs_below_count_ = 0;
            speech_frames_ = 0;
            return kSpeechStart;
        }
        return kNone;
    }

    speech_frames_++;
    if (below_count_ >= kEndFrames || abs_below_count_ >= kAbsSilenceFrames ||
        speech_frames_ >= kMaxSpeechFrames) {
        speaking_ = false;
        above_count_ = 0;
        below_count_ = 0;
        abs_below_count_ = 0;
        speech_frames_ = 0;
        return kSpeechEnd;
    }
    return kSpeechContinue;
}
