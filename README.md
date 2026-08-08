# ESP32-S3 AI 助手

> 硬件：ESP32-S3-DevKitC-1-N16R8（16MB Quad SPI Flash + 8MB Octal PSRAM，注意不是 Octal Flash）+ 2.8" ILI9341 SPI 触摸屏 + INMP441 麦克风 + MAX98357A 功放 + 3W/8Ω 喇叭
> 框架：PlatformIO + Arduino，当前版本 **v1.0.0**

## 功能速览

- 黑底绿色 hack 风格 OS：Chat / Terminal / Settings / WiFi / Apps / Monitor
- 云端 LLM 聊天（默认 Agnes AI，支持多供应商），聊天区可滑动、可开新对话
- 终端：`ping`、`ipconfig`、`wifi`、`play` 等，屏幕键盘带符号
- 天气：Open-Meteo 当前天气 + 3 天预报，右侧动态天气图标
- WiFi：扫描 / 历史密码直连 / AP 网页配网 / BLE 配网 / 信号分析仪
- 音乐：本地 `data/music/*.wav` + 云端 MP3 歌单（`data/cloud.txt`）
- 通断测试仪：GPIO1/GPIO2 测导线通断，OK/OPEN + 蜂鸣
- Monitor 性能页：电压 ADC、温度、Heap/PSRAM/Flash、CPU、RSSI、MAC、Uptime
- 语音：Mic Test 回放、Bcut ASR 语音对话、Edge TTS 中文播报
- OTA：`ota <https-url>` 或 AP 网页 `/api/update?url=` 双分区流式升级，断电可回滚
- 俄罗斯方块、Logs 实时日志、BLE 设备扫描

## 最终接线（重要）

**供电总原则：全部只接 3V3，不使用 5V。**

| 电源 | 接法 |
|---|---|
| 3V3 #1 | TFT VCC + TFT LED（两个脚并联） |
| 3V3 #2 | INMP441 VCC + MAX98357A VIN + SD（并联） |
| GND | TFT / Touch / MAX98357A / INMP441 / 喇叭 共地 |

| 模块 | 引脚 | ESP32 |
|---|---|---|
| TFT | VCC, LED | 3V3（并联） |
| TFT | GND | GND |
| TFT | CS, RESET, DC, SDI, SCK | 15, 14, 16, 11, 12 |
| Touch | T_CS, T_IRQ, T_CLK, T_DIN, T_DO | 5, 4, 6, 7, 9 |
| INMP441 | VCC, GND | 3V3, GND |
| INMP441 | SCK, WS, SD | 41, 42, 40 |
| INMP441 | L/R | GND（必接，选左声道） |
| MAX98357A | VIN, GND | 3V3, GND |
| MAX98357A | BCLK, LRC, DIN | 18, 8, 17 |
| MAX98357A | SD | 必须与 VIN 并联到 3V3，悬空无声 |
| MAX98357A | GAIN | 可悬空（默认 9dB）或接 GND（12dB） |
| 喇叭 | SPK+/SPK- | 接功放，不可直连 GPIO |
| RGB LED | GPIO48 | 预留 |
| 通断探针 | Probe A / Probe B | GPIO1 / GPIO2 |
| 电压监测 | VIN-Sense | GPIO10（需外接分压电阻） |

**电压监测说明：**

- 固件默认用 GPIO10 做 ADC 采样，`VOLT_SENSE_PIN` 可改。
- 直接量 3.3V 时需分压（如 10k + 10k），否则裸 ADC 读数约 0.1-0.4V 不准确。
- `VOLT_SENSE_SCALE` 为分压换算系数：等比分压写 `2.0`。
- Monitor 页和 Continuity 页都会显示；串口输入 `monitor` 直达。

## 已确认踩坑

1. **5V 排针坑**：本克隆板正面有 `IN-OUT` 焊盘，出厂开路，5V 排针没有 USB 5V（实测约 1.8V）。屏幕/功放接 5V 不工作多半是这个问题；现在统一只用 3V3，不再使用 5V。
2. **麦克风 L/R 必须接 GND**，悬空会无数据/杂音。
3. **MAX98357A SD 必须接 VIN**，悬空功放关闭、无声。
4. **Edge TTS 不能用 16kHz 输出格式**（服务端只回 turn.start 不给音频），必须用 24kHz；当前使用 `audio-24khz-96kbitrate-mono-mp3` 提高音质，24k→16k 由固件重采样。
5. **Bcut ASR 上传流程**：先 `resource/create` 拿上传地址，再用 PUT 上传音频，`Etag` 必须 `HTTPClient.collectHeaders()` 才能读到。
6. **文件系统已改为 LittleFS**（约 7.9MB），长文件名可用；首次刷入需重新 `uploadfs`，旧的 SPIFFS 内容会丢失。
7. **ESP32-S3 只支持 2.4GHz WiFi**；路由器开 WiFi6-only / WPA3-only / 双频合一可能导致扫到却连不上。CMCC 光猫优先关“智能组网/双频合一”。
8. **触摸校准固化值**：`X: 362-3914`、`Y: 150-3722`、`swapXY=1`、`invX=1`、`invY=1`。新增按钮必须沿用 v2.0.0 触摸命中逻辑，不要随意改布局。
9. TTS 说话时 Chat 操作栏 `Voice` 会变成 `Stop`，点击可中断朗读。
10. 语音对话前固件会自动重连已保存 WiFi，AP 模式下也能用 `voice` 命令触发重连。

## 配网方式

### AP 网页配网（推荐，手机最方便）

1. 连接热点 `ESP32-AI-Setup / 12345678`
2. 打开 `http://192.168.4.1`
3. 填写 SSID + 密码，保存后自动重启连接

### BLE 配网

1. 先把设备置于配网模式（未保存 WiFi 开机自动进入 AP，或 Settings → Start WiFi AP），再用 nRF Connect / LightBlue 连接 `ESP32-AI-Setup`
2. 服务 UUID：`6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
3. 向 RX 特征 `6E400002-...` 写入 `WiFiSSID:WiFi密码`
4. 设备回复 `OK:SAVED` 后自动重启

## 常用串口命令

```text
chat / term / settings / wifi / weather / wifi map / music / tetris
home / apps / logs / ble / continuity / voice
monitor / info               # 性能监视页（电压/温度/内存等）
say 你好                    # 中文 TTS
tts stop                    # 中断当前 TTS 朗读
record                      # 麦克风录 3 秒回放
voice                       # 语音对话（录音->ASR->LLM->TTS）
play 0 / stop / vol 70 / vol+ / vol-
ipconfig / ping baidu.com
wifi set <ssid> <pass> / wifi history / wifi set <ssid> <pass>
provider / provider 0 / provider agnes
calib / calib show / calib reset
tap <x> <y> / screen / reboot
```

任意非命令文字会直接进入 Chat 并发送给 LLM。

## OS 页面

- 首页：Chat / Terminal / Settings / WiFi / Apps / Monitor，右上角时间
- Chat 操作栏：`New` 新对话、`Up/Down` 滚动、`Input` 收起键盘、`Speak` 朗读上次回复、`Voice` 语音对话（说话时变 `Stop`）
- Settings：4 点校准、重置校准、Mic Test、Voice Chat、Start AP、Next Provider、System Info、BLE Scan、Continuity
- Weather：右侧动画图标，`Refresh` 手动刷新
- Music：本地 WAV + 云端 `[C]` MP3，音量可调
- Continuity：GPIO1/GPIO2 测线，`Beep` 可关

## 云端配置

`src/config.h` 被 git 忽略，先复制 `src/config.example.h` 并填写：

```cpp
#define LLM_AGNES_KEY  "sk-xxxx"
#define TTS_MODE       2       // 0=ResponsiveVoice, 1=Edge TTS 代理, 2=Edge 直连(默认)
#define TTS_VOICE      "zh-CN-XiaoxiaoNeural"
#define WEATHER_CITY   "Nanchang"
#define WEATHER_LAT    28.684
#define WEATHER_LON    115.858
```

- 默认 LLM 供应商为 Agnes AI（`agnes-2.5-flash`），国内可直连
- LLM 系统提示词要求按 `EN:`（屏幕英文摘要）+ `ZH:`（TTS 中文正文）返回
- TTS 默认 Edge 直连 WebSocket，无需 Key；播放时边收边解，支持 `Stop`
- 天气使用 Open-Meteo，免费无 Key
- 云端音乐歌单：`data/cloud.txt` 每行 `名称|https://直链.mp3`，上传用 `pio run -t uploadfs --upload-port COM3`

## 安全基线（量产注意）

- 所有 HTTPS 出站连接使用固件内置 Mozilla CA 捆绑包校验证书，不再 `setInsecure()`。
- 网页 API（含 `/api/chat`、`/api/update`）与 BLE 配网服务只在 AP 配网模式开启，STA 联网时关闭，防止局域网内被任意调用烧 API 额度。
- 串口/屏幕日志不再打印 WiFi 密码；`wifi history` 显示掩码。
- `src/config.h` 里的 LLM API Key 属于明文固件内置，**该 Key 已在本地出现，请立即在服务商后台轮换**；新 Key 不要提交到任何仓库。彻底方案是迁移到 ESP-IDF 并开启 Flash 加密 + Secure Boot。

## 开发与烧录

```text
pio run -e esp32s3                       # 编译
pio run -e esp32s3 -t upload --upload-port COM3
pio run -e esp32s3 -t uploadfs --upload-port COM3   # 上传 LittleFS(音乐/歌单)
pio device monitor -p COM3 -b 115200
```

串口走 UART0（GPIO43/44），本机通常枚举为 COM3；不要插 ESP32-S3 原生 USB 口。
`src/config.h`、`data/cloud.txt`、`data/music/`、`music_source/` 均不进入 git。

OTA 升级（双分区 app0/app1，带断电回滚保护）：

```text
ota https://your-server/firmware.bin
```

升级失败或写入中断时旧固件不受影响；新固件启动后完成外设初始化才 `mark valid`，
崩溃/中途断电会自动回滚上一版本。AP 配网页面也可用 `GET /api/update?url=<https-url>` 触发。

## 已知限制

- 屏幕无中文字体，OS 全英文；中文由 TTS 朗读
- 未实现“小度小度”离线唤醒词（免费 ESP-SR 内置词表不含该词），当前用按钮/`voice` 命令触发
- 语音识别使用 Bcut 免费云接口，依赖 WiFi；无网络时无法识别
- `ping` 是 TCP 80/443 连通性测试，不是 ICMP

## 工程结构

```text
src/
├── os.cpp          # 主界面/按钮/串口命令/页面逻辑
├── ota.cpp         # HTTPS 流式 OTA + bootloader 回滚确认
├── voice.cpp       # 录音 + Bcut ASR 语音识别
├── tts.cpp         # Edge TTS 直连 + Helix 解码（后台任务，可停止）
├── audio.cpp       # I2S 输出/输入、蜂鸣、WAV/PCM 播放
├── music.cpp       # 本地 WAV + 云端 MP3 流式播放
├── weather.cpp     # Open-Meteo 天气
├── wifi_utils.cpp  # WiFi 连接/重连/历史/AP
├── ble_provision.cpp # BLE 配网
├── touch.cpp / touch_calib.cpp
├── tetris.cpp / web_server.cpp / llm.cpp / logger.cpp
├── cert_bundle.cpp # 内置 Mozilla CA 证书捆绑包 (tools/gen_crt_bundle.py 生成)
└── config.h        # 所有引脚/API Key/参数（git 忽略）
```
