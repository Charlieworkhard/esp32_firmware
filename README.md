# esp32_firmware

ESP32-S3 语音助手固件（“小Y”）：麦克风采集 → WebSocket → 自建后端 → 播放合成语音，配 OLED 状态显示。

详细设计见 [docs/DESIGN.md](docs/DESIGN.md)。

## 硬件

- ESP32-S3（N16R8，16MB Flash + 8MB PSRAM）
- INMP441 I2S 麦克风
- MAX98357 I2S 功放
- SSD1306 0.91" OLED（128x32，I2C）
- BOOT 按键
- 音量加/减按键

## 引脚

| 功能 | GPIO |
| --- | --- |
| 麦克风 WS | 4 |
| 麦克风 SCK | 5 |
| 麦克风 DIN | 6 |
| 功放 BCLK | 15 |
| 功放 LRCK | 16 |
| 功放 DOUT | 7 |
| OLED SDA | 41 |
| OLED SCL | 42 |
| BOOT 键 | 0 |
| 音量 + | 40 |
| 音量 − | 39 |

麦克风和功放各用一个 I2S 控制器（收发分离），引脚集中在 `main/config.h`。

## 前置条件

- ESP-IDF **v6.1**（`idf.py --version` 确认）
- 目标芯片 `esp32s3`，16 MB Flash / 8 MB PSRAM

## 构建

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

> `sdkconfig`、`build/`、`managed_components/` 都不在仓库里：首次构建会按 `sdkconfig.defaults` 自动生成配置，并把 `dependencies.lock` 里锁定的组件拉下来。改了 `sdkconfig.defaults` 后需要 `idf.py fullclean` 或删掉 `sdkconfig` 再构建。

## 首次使用

1. 上电后若无 Wi-Fi 记录，设备开启热点 `XiaoY-XXXX`
2. 手机连接热点，浏览器打开 `http://192.168.4.1`
3. 填写 2.4GHz Wi-Fi 账号密码，保存后设备自动重启联网

## 配置

`idf.py menuconfig` → `Application Configuration`：

- `APP_ASSISTANT_NAME`：助手名称（默认 `小Y`）
- `APP_AP_SSID_PREFIX`：配网热点前缀（默认 `XiaoY`）
- `APP_CONFIG_URL`：启动时拉取服务器配置的地址

## 交互

- 按 BOOT：待命中开麦；聆听中再按一次关麦；播放中按则打断回复并关麦
- 开麦后直接说话，静音 960ms 自动结束这一轮并发送
- 回复播完后的 3 秒内可以直接接着说话（免按键连聊），超过 3 秒需按 BOOT
- 音量键（GPIO39 减 / GPIO40 加）：单步 10，范围 10~100，重启后保留
