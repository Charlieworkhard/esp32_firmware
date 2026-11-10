#include "opus_codec.h"

#include <esp_log.h>
#include <esp_opus_dec.h>
#include <esp_opus_enc.h>

#define TAG "OpusCodec"

OpusCodec::~OpusCodec() {
    Close();
}

bool OpusCodec::Init() {
    if (encoder_ != nullptr || decoder_ != nullptr) {
        return true;
    }

    esp_opus_enc_config_t encoder_config = ESP_OPUS_ENC_CONFIG_DEFAULT();
    encoder_config.sample_rate = kSampleRate;
    encoder_config.channel = ESP_AUDIO_MONO;
    encoder_config.bits_per_sample = ESP_AUDIO_BIT16;
    encoder_config.bitrate = 32000;
    encoder_config.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    encoder_config.application_mode = ESP_OPUS_ENC_APPLICATION_VOIP;
    encoder_config.complexity = 5;
    encoder_config.enable_fec = false;
    encoder_config.enable_dtx = false;
    encoder_config.enable_vbr = false;

    esp_audio_err_t err =
        esp_opus_enc_open(&encoder_config, sizeof(encoder_config), &encoder_);
    if (err != ESP_AUDIO_ERR_OK || encoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to open encoder: %d", err);
        encoder_ = nullptr;
        return false;
    }

    int in_size = 0;
    int out_size = 0;
    esp_opus_enc_get_frame_size(encoder_, &in_size, &out_size);
    encode_frame_samples_ = in_size / sizeof(int16_t);
    encode_output_size_ = out_size;

    esp_opus_dec_cfg_t decoder_config = ESP_OPUS_DEC_CONFIG_DEFAULT();
    decoder_config.sample_rate = kSampleRate;
    decoder_config.channel = ESP_AUDIO_MONO;
    decoder_config.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS;
    decoder_config.self_delimited = false;

    err = esp_opus_dec_open(&decoder_config, sizeof(decoder_config), &decoder_);
    if (err != ESP_AUDIO_ERR_OK || decoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to open decoder: %d", err);
        decoder_ = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "Opus ready: in %d samples, out %d bytes", encode_frame_samples_,
             encode_output_size_);
    return true;
}

void OpusCodec::Close() {
    if (encoder_ != nullptr) {
        esp_opus_enc_close(encoder_);
        encoder_ = nullptr;
    }
    if (decoder_ != nullptr) {
        esp_opus_dec_close(decoder_);
        decoder_ = nullptr;
    }
}

int OpusCodec::Encode(const int16_t* pcm, uint8_t* output, int output_capacity) {
    if (encoder_ == nullptr) {
        return 0;
    }

    esp_audio_enc_in_frame_t in_frame = {};
    in_frame.buffer = reinterpret_cast<uint8_t*>(const_cast<int16_t*>(pcm));
    in_frame.len = kFrameSamples * sizeof(int16_t);

    esp_audio_enc_out_frame_t out_frame = {};
    out_frame.buffer = output;
    out_frame.len = output_capacity;

    esp_audio_err_t err = esp_opus_enc_process(encoder_, &in_frame, &out_frame);
    if (err != ESP_AUDIO_ERR_OK) {
        return 0;
    }
    return static_cast<int>(out_frame.encoded_bytes);
}

int OpusCodec::Decode(const uint8_t* data, int size, int16_t* pcm, int pcm_capacity) {
    if (decoder_ == nullptr) {
        return 0;
    }

    esp_audio_dec_in_raw_t raw = {};
    raw.buffer = const_cast<uint8_t*>(data);
    raw.len = size;

    esp_audio_dec_out_frame_t out_frame = {};
    out_frame.buffer = reinterpret_cast<uint8_t*>(pcm);
    out_frame.len = pcm_capacity * sizeof(int16_t);

    esp_audio_dec_info_t info = {};
    esp_audio_err_t err = esp_opus_dec_decode(decoder_, &raw, &out_frame, &info);
    if (err != ESP_AUDIO_ERR_OK) {
        return 0;
    }
    return static_cast<int>(out_frame.decoded_size / sizeof(int16_t));
}
