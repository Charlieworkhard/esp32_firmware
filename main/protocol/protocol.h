#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct cJSON;

struct AudioPacket {
    std::vector<uint8_t> payload;
    uint32_t timestamp = 0;
};

// 与服务器之间的文本 + 二进制帧通道。
class Protocol {
public:
    using JsonCallback = std::function<void(const cJSON* root)>;
    using AudioCallback = std::function<void(std::unique_ptr<AudioPacket> packet)>;
    using ConnectionCallback = std::function<void(bool connected)>;

    virtual ~Protocol() = default;

    virtual bool Start(const std::string& url, const std::string& token) = 0;
    virtual void Stop() = 0;
    virtual bool IsConnected() const = 0;

    virtual bool SendAudio(const uint8_t* data, size_t size, uint32_t timestamp) = 0;
    virtual bool SendJson(cJSON* root) = 0;

    bool SendStartListening(const std::string& mode = "auto");
    bool SendStopListening();
    bool SendAbortSpeaking();

    void OnIncomingJson(JsonCallback callback) { on_json_ = std::move(callback); }
    void OnIncomingAudio(AudioCallback callback) { on_audio_ = std::move(callback); }
    void OnConnectionChanged(ConnectionCallback callback) { on_connection_ = std::move(callback); }

protected:
    JsonCallback on_json_;
    AudioCallback on_audio_;
    ConnectionCallback on_connection_;
};

#endif  // PROTOCOL_H
