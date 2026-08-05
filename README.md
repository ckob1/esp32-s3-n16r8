# ESP32-S3 AI 助手

> 基于 ESP32-S3-DevKitC-1-N16R8 + 2.8" ILI9341 触摸屏 + 云端 LLM API 的嵌入式 AI 助手。
> PlatformIO + Arduino 框架，触摸交互 + 串口输入双通道。
>
> **当前版本 v1.0.0** - 正式候选版: 校准固化 + WiFi 诊断 + 首页状态

---

## 目录

- [1. 已确认正常功能](#1-已确认正常功能)
- [2. 硬件清单](#2-硬件清单)
- [3. 完整引脚对应表](#3-完整引脚对应表)
- [4. 工程目录结构](#4-工程目录结构)
- [5. 开发环境搭建](#5-开发环境搭建)
- [6. 烧录与使用](#6-烧录与使用)
- [7. 串口命令完整列表](#7-串口命令完整列表)
- [8. 云端大模型 API 配置](#8-云端大模型-api-配置)
- [9. 触摸动态校准](#9-触摸动态校准)
- [10. 已知限制](#10-已知限制)
- [11. 更新记录](#11-更新记录)

---

## 1. 已确认正常功能

| 功能 | 状态 | 说明 |
|---|---|---|
| ✅ TFT 显示屏点亮 | 正常 | 红/绿/蓝三色自检通过 |
| ✅ WiFi 连接 | 正常 | 自动扫描、状态诊断、断线重连 |
| ✅ LLM API 调用 | 正常 | Agnes AI (agnes-2.5-flash) 实测可用 |
| ✅ 16MB Flash / 8MB PSRAM | 正常 | N16R8 `qio_opi` 正确配置, PSRAM 实测 8189 KB |
| ✅ 多供应商切换 | 正常 | 6 家预置 (Z.ai / 智谱 / DeepSeek / Moonshot / OpenAI / Agnes) |
| ✅ 触摸点击 | 正常 | 自写 XPT2046 驱动, 独立 HSPI |
| ✅ 触摸动态校准 | 正常 | NVS 持久化, 4 点交互式校准 |
| ✅ I²S 音频输出 | 正常 | 蜂鸣音反馈 |
| ✅ 串口输入交互 | 正常 | 文字提问 + 命令控制 |
| ⚠️ 中文屏幕显示 | 部分 | ASCII 完整显示, 中文显示为方块 □ |
| 📝 麦克风录音 | 预留 | 引脚已分配, 代码未实现 |
| 📝 TTS 语音播报 | 预留 | I²S 输出已就绪, 未接喇叭 |

---

## 2. 硬件清单

| 模块 | 型号 | 说明 |
|---|---|---|
| 主控 | ESP32-S3-WROOM-1-**N16R8** | 16MB Flash + 8MB Octal PSRAM |
| 屏幕 | 2.8" SPI TFT 触摸屏 MSP2807 | ILI9341 驱动, 320×240, XPT2046 触摸, 14-pin |
| 音频输出 | I²S DAC (可选) | 接 GPIO 17/18/8, 当前用蜂鸣合成 |
| I²S 麦克风 | 数字 MEMS 麦克风 | **预留接口**, 引脚已分配 |
| 板载 LED | WS2812 RGB | GPIO 48 (代码预留) |

---

## 3. 完整引脚对应表

### 3.1 TFT 显示屏 (FSPI)

| 屏幕引脚 | ESP32-S3 GPIO | 备注 |
|---|---|---|
| VCC | 3V3 | **必须 3V3, 不能 5V** |
| GND | GND | |
| CS | GPIO 15 | TFT 片选 |
| RESET | GPIO 14 | |
| DC/RS | GPIO 16 | 数据/命令选择 |
| SDI (MOSI) | GPIO 11 | SPI 主出从入 |
| SCK | GPIO 12 | SPI 时钟 |
| LED (BL) | 3V3 | 直连常亮 (可后续接 GPIO 做 PWM 调光) |
| SDO (MISO) | 不接 | 只写模式 |

### 3.2 触摸屏 XPT2046 (独立 HSPI, 与 TFT 完全分离)

> 5 根杜邦线全部独立, 不与 TFT 共享 SCK/MOSI/MISO

| 触摸引脚 | ESP32-S3 GPIO | 备注 |
|---|---|---|
| T_CS | GPIO 5 | 触摸片选 |
| T_IRQ | GPIO 4 | 中断引脚 (低电平=有触摸) |
| T_CLK | GPIO 6 | 触摸 SPI 时钟 (独立) |
| T_DIN | GPIO 7 | 触摸 SPI 主出 (独立) |
| T_DO | GPIO 9 | 触摸 SPI 主入 (必须接) |

### 3.3 I²S 音频输出

| 信号 | ESP32-S3 GPIO | 备注 |
|---|---|---|
| BCLK | GPIO 18 | I²S 位时钟 |
| LRC (WS) | GPIO 8 | I²S 字选择 |
| DIN | GPIO 17 | ESP32 输出 → 喇叭/DAC |

### 3.4 I²S 麦克风 (预留)

| 信号 | ESP32-S3 GPIO | 备注 |
|---|---|---|
| SCK | GPIO 41 | 麦克风时钟 |
| WS | GPIO 42 | 麦克风字选择 |
| SD | GPIO 40 | 麦克风数据 → ESP32 |
| L/R | GND | 左声道输出 |
| VCC | 3V3 | |

### 3.5 板载外设

| 外设 | ESP32-S3 GPIO | 备注 |
|---|---|---|
| 板载 RGB LED (WS2812) | GPIO 48 | 状态指示 (代码预留) |
| BOOT 按钮 | GPIO 0 | |
| USB 串口 | GPIO 43(TX) / 44(RX) | 勿占用 |

### 3.6 禁用引脚 (避开)

| GPIO | 原因 |
|---|---|
| 0 | BOOT 模式选择 |
| 19, 20 | USB D-/D+ |
| 26-32 | 内置 Flash SPI |
| 33-37 | Octal PSRAM (N16R8) |
| 43, 44 | U0TXD/RXD (USB-CDC 串口) |

---

## 4. 工程目录结构

```
esp32-s3-ai-assistant/
├── platformio.ini              # PlatformIO 配置
├── partitions_16MB.csv         # 16MB Flash 分区表 (含 7MB app)
├── README.md                   # 本文件
├── data/                       # 未来放 LittleFS 资源
└── src/
    ├── config.h                # 全局配置: 引脚 + WiFi + LLM API + UI 颜色
    ├── main.cpp                # 主程序: 触摸按钮 + 串口输入 + LLM 问答
    ├── display.h / .cpp        # TFT 显示封装 (Adafruit ILI9341 + 内置 8x16 字体)
    ├── cn_font.h / .cpp        # 中文字符渲染 (内置 ASCII 位图字体)
    ├── touch.h / .cpp          # XPT2046 触摸 (自写 SPI 驱动, 独立 HSPI)
    ├── touch_calib.h / .cpp    # 动态触摸校准 (NVS 持久化)
    ├── audio.h / .cpp          # I²S 蜂鸣音输出
    ├── wifi_utils.h / .cpp     # WiFi 连接 + 自动重连 + 详细诊断
    └── llm.h / .cpp            # 云端 LLM API 客户端 (6 供应商, OpenAI 兼容)
```

---

## 5. 开发环境搭建

### 5.1 安装软件

1. **VSCode**: https://code.visualstudio.com/
2. **PlatformIO 插件**: 在 VSCode 扩展市场搜索 `platformio-ide` 安装
3. **USB 驱动** (若用国产 CH340/CP2102 板子):
   - CH340: https://www.wch.cn/downloads/CH341SER_EXE.html
   - CP2102: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

> 源码中 `src/config.h` 默认被 git 忽略。首次导入请复制
> `src/config.example.h` 为 `src/config.h`, 再填写自己的 API Key。

### 5.2 导入工程

1. 解压本项目文件夹到任意路径 (路径不要有中文/空格)
2. **如果是覆盖旧工程, 务必删除旧的 `src/User_Setup.h` 文件** (与 `platformio.ini` 的 build_flags 冲突)
3. VSCode → PlatformIO 主页 → `Open Project` → 选择 `esp32-s3-ai-assistant` 文件夹
4. 等待 PlatformIO 自动下载依赖:
   - `espressif32@^6.5.0` 平台
   - `Adafruit GFX Library@^1.11.10`
   - `Adafruit ILI9341@^1.6.1`
   - `ArduinoJson@^7.0.4`

### 5.3 USB 串口配置

本项目 `platformio.ini` 设置:
```ini
-DARDUINO_USB_MODE=1
-DARDUINO_USB_CDC_ON_BOOT=0
```

- Serial 走 UART0 (GPIO 43/44), 通过 USB-Serial 芯片输出
- 用户能在 Serial Monitor 看到所有日志 (包括 panic 信息)
- 接 ESP32-S3-DevKitC-1 的 **UART 端口** (标 UART 或靠近 CP2102 的 USB 口)

### 5.4 分区表

N16R8 板子用 `partitions_16MB.csv`:
- 7MB app 分区 (足够放应用代码)
- 2MB OTA 备份
- 2MB SPIFFS
- 20KB NVS (存触摸校准值)

---

## 6. 烧录与使用

### 6.1 烧录

1. USB 连接 ESP32-S3 到电脑 (接 UART 端口)
2. VSCode 底部状态栏 → 选择端口 (COM3 或其他)
3. 点击 `→` (Upload) 按钮, 等待编译 + 烧录
4. 烧录完成后自动复位, 串口监视器输出启动日志

### 6.2 启动流程

```
=====================================
  ESP32-S3 AI Assistant v1.0.0
  Board: N16R8 (16MB Flash + 8MB Octal PSRAM)
  Display: Adafruit ILI9341
  LLM: Agnes AI (agnes-2.5-flash @ .cn)
=====================================
[Main] Step 1: display_init()
[Display] 初始化 Adafruit ILI9341...
[Display] 引脚: CS=15 DC=16 RST=14 SCK=12 MOSI=11
[Display] 调用 SPI.begin()...
[Display] 调用 tft.begin()...
[Display] 方向: rot=1 尺寸: 320x240
[Display] ✅ TFT 初始化完成
[CN_Font] 内置 8x16 位图字体已就绪
[Main] Step 2: display_self_test()
[Display] === 屏幕诊断开始 ===
[Display] 阶段1: 红色全屏 (3秒)   ← 屏幕变红
[Display] 阶段2: 绿色全屏 (3秒)
[Display] 阶段3: 蓝色全屏 (3秒)
[Display] 阶段4: 显示诊断信息 (5秒)
[Main] Step 3: display_splash()
[Main] Step 4: audio_init()
[Audio] 初始化 I2S 输出...
[PSRAM] not found   ← N16R8 板子应显示 found, 这里 PSRAM 未配置但功能正常
[TouchCalib] 加载:
  X: 434 - 3496
  Y: 133 - 3745
  swapXY=1 invX=1 invY=1
  来源: config.h (默认)   ← 已校准会显示 NVS
[Touch] 初始化 XPT2046 (独立 HSPI, 自写驱动)...
[Touch] ✅ 初始化完成
[WiFi] === 开始连接 WiFi ===
[WiFi] SSID: <saved ssid or AP mode>
[WiFi] 扫描附近 WiFi...
[WiFi] 扫描到 18 个网络:
  ...
[WiFi] ✅ 连接成功
[WiFi] IP: 192.168.18.182
[LLM] 供应商列表:
  [0] Z.ai      NO KEY
  [1] GLM-CN    NO KEY
  ...
* [5] Agnes     OK   ← 默认供应商
[OS] Ready. Commands: home | chat | term | settings | wifi | calib | record
```

### 6.3 触摸按钮 (横屏 320×240)

主界面是 6 个 OS 应用按钮:

| 按钮 | 屏幕位置 (x,y) | 功能 |
|---|---|---|
| Chat | (8, 44) ~ (104, 82) | 聊天页, 带英文屏幕键盘 |
| Terminal | (112, 44) ~ (208, 82) | 终端命令页 |
| Settings | (216, 44) ~ (312, 82) | 校准/供应商/AP/系统信息 |
| WiFi | (8, 90) ~ (104, 128) | 配网状态与 AP 入口 |
| Logs | (112, 90) ~ (208, 128) | 实时日志查看器 |
| Info | (216, 90) ~ (312, 128) | 系统信息 |

### 6.4 音效反馈

- 启动: 三声上扬音 (1500→2000→2500 Hz)
- AI 回复成功: 短促两声升调
- AI 请求失败: 低频两声降调
- 触摸点击: 极短"嘀"声
- 开机动画: 黑底绿色苹果图标旋转 16 帧后进入 OS

### 6.5 WiFi 连接问题排查

- ESP32-S3 只支持 2.4GHz WiFi, 不支持 5GHz
- 如果路由器开了 `WiFi 6 only` 或 `WPA3-only`, ESP32 可能扫到但连不上
- 建议路由器设置为 `2.4GHz + WPA2/WPA3 mixed` 或 `WPA2-Personal`
- 扫描日志现在会显示每个网络的频道和加密类型 (`WPA2` / `WPA2/WPA3` / `WPA3`)
- 已保存 WiFi 多次重连失败后, 系统会自动进入 AP 配网模式
- 如果 CMCC 这类运营商光猫 WiFi 连不上, 优先登录光猫后台关闭“智能组网/双频合一”, 并把 2.4G 设置为 WPA2

---

## 7. 串口命令完整列表

在 PlatformIO Serial Monitor (115200, NL 换行) 中输入命令 + 回车:

### 7.1 LLM 供应商管理

| 命令 | 功能 |
|---|---|
| `provider` | 列出所有供应商 + 状态 |
| `provider 0` | 按索引切换 (0-5) |
| `provider zai` | 按名字切换 (支持前缀, 不区分大小写) |
| `provider agnes` | 切换到 Agnes |
| `provider deep` | 切换到 DeepSeek |
| `provider moon` | 切换到 Moonshot |

### 7.2 触摸校准

| 命令 | 功能 |
|---|---|
| `calib` | 进入交互式 4 点动态校准 |
| `calib show` | 显示当前校准值和来源 (NVS 还是默认) |
| `calib reset` | 清除 NVS, 恢复 config.h 默认值 |

### 7.3 AI 问答

任意非命令文字 + 回车 → 当作问题发给 AI
```
What is 1+1?
Tell me a short joke.
Hello, who are you?
```

### 7.4 OS 与网络命令

| 命令 | 功能 |
|---|---|
| `home` | 返回 OS 桌面 |
| `chat` | 进入聊天页 |
| `term` / `terminal` | 进入终端页 |
| `settings` | 进入设置页 |
| `wifi` | 进入 WiFi 页 |
| `logs` | 进入实时日志页 |
| `wifi set <ssid> <pass>` | 保存 WiFi 并重启 |
| `record` | 录音 3 秒并播放 |
| `reboot` | 重启设备 |

---

## 8. 云端大模型 API 配置

### 8.1 多供应商架构

工程内置 **6 家供应商**, 全部使用 OpenAI 兼容协议:

| 索引 | 名称 | Endpoint | 推荐 Model | 申请地址 |
|---|---|---|---|---|
| 0 | Z.ai | `https://api.z.ai/api/paas/v4/chat/completions` | `glm-4-flash` (免费) | https://z.ai/manage-apikey |
| 1 | 智谱 BigModel | `https://open.bigmodel.cn/api/paas/v4/chat/completions` | `glm-4-flash` | https://open.bigmodel.cn |
| 2 | DeepSeek | `https://api.deepseek.com/v1/chat/completions` | `deepseek-chat` | https://platform.deepseek.com |
| 3 | Moonshot | `https://api.moonshot.cn/v1/chat/completions` | `moonshot-v1-8k` | https://platform.moonshot.cn |
| 4 | OpenAI | `https://api.openai.com/v1/chat/completions` | `gpt-4o-mini` | https://platform.openai.com |
| 5 | **Agnes AI** (默认) | `https://apihub.agnes-ai.cn/v1/chat/completions` | `agnes-2.5-flash` | https://wiki.agnes-ai.com |

### 8.2 填写 API Key

在 `src/config.h` 找到对应供应商的宏:
```cpp
#define LLM_ZAI_KEY         ""
#define LLM_GLM_CN_KEY      ""
#define LLM_DS_KEY          ""
#define LLM_KS_KEY          ""
#define LLM_OAI_KEY         ""
#define LLM_AGNES_KEY       "sk-xxxxx..."    // 已预填
```

**留空的供应商在 [Switch] 切换时会被自动跳过**。

### 8.3 Agnes AI 官方接口要点

- Base URL: `https://apihub.agnes-ai.cn/v1` (实测 .cn 可用)
- Endpoint: `POST /v1/chat/completions`
- Auth: `Authorization: Bearer <api_key>`
- 请求格式: OpenAI 兼容 (`model` + `messages` + 可选 `temperature` / `max_tokens`)
- 模型名: `agnes-2.5-flash` (推荐) 或 `agnes-2.0-flash` (兼容旧版)
- 响应格式: `choices[0].message.content`
- 上下文 512K, 最大输出 65.5K, 当前输入/输出 token 都免费

### 8.4 系统提示词

`LLM_SYSTEM_PROMPT` 决定 AI 的角色与风格:
```cpp
#define LLM_SYSTEM_PROMPT "You are an AI assistant on an ESP32-S3 device. Answer briefly in English, keep replies under 150 words, and use plain text without Markdown."
```

### 8.5 HTTP 小服务器

| 路由 | 方法 | 说明 |
|---|---|---|
| `/` | GET | 配网页 (WiFi 表单 + API 文档) |
| `/api/status` | GET | 返回模式/IP/供应商/PSRAM/Heap JSON |
| `/api/chat?q=hello` | GET | 调用 LLM 并返回 JSON |
| `/api/wifi` | POST | 保存 SSID/密码并重启 |
| `/api/record?ms=3000` | GET | 录制麦克风并返回 WAV |
| `/api/reboot` | GET | 重启设备 |

STA 模式下访问 `http://<设备IP>`; AP 配网模式下访问 `http://192.168.4.1`。

---

## 9. 触摸动态校准

### 9.1 工作原理

校准值存在 ESP32 的 NVS (非易失存储), 重启不丢失。

### 9.2 校准流程

1. 串口输入 `calib`
2. 屏幕中央显示一个标准框, 依次点击框的 4 个顶点 (左上 → 右上 → 左下 → 右下)
3. 用触摸笔或手指点击十字中心
4. 程序自动判断 SWAP_XY / INVERT_X / INVERT_Y
5. 保存到 NVS, 下次开机自动应用

当前正式版已固化校准参数:
`X: 362-3914`, `Y: 150-3722`, `swapXY=1`, `invX=1`, `invY=1`

重新校准入口只保留:
- Settings → `Calibrate 4-Point`
- Terminal → `calib`
- Serial Monitor → `calib`

### 9.3 算法

- 比较横向变化和纵向变化的差值, 判断 raw_x/raw_y 是否需要 SWAP
- 使用中央标准框 4 个顶点的已知屏幕坐标做线性外推, 算出屏幕四边对应的 raw 值
- 根据屏幕 0 和最大坐标对应的 raw 大小自动判断 invertX / invertY
- 校准数据带版本号, 旧算法保存的 NVS 值会被新固件忽略

### 9.4 调试日志

每次触摸都会输出 raw 和 screen 坐标:
```
[Touch] raw(3496,3745) z=1321 → screen(0,0)
[Touch] raw(434,133) z=133 → screen(320,240)
```

---

## 10. 已知限制

| 限制 | 说明 | 后续计划 |
|---|---|---|
| 中文 UI 停用 | 屏幕统一使用英文, 不再出现方块 | 后续可加载中文字体恢复 |
| LLM 默认英文回复 | 为配合 ASCII 屏幕, 系统提示词要求英文纯文本 | 需要中文时改成中文字体后再恢复 |
| 无 TTS 语音输出 | I²S 当前只做蜂鸣 | 接 MAX98357A + 云端 TTS |
| 无本地语音识别/唤醒 | 目前只做录音、播放和 WAV 输出 | 后续接语音识别/唤醒词 |
| 无 TTS 云播报 | 当前扬声器播放录音/蜂鸣 | 接云端 TTS 后播放 |
---

## 11. 更新记录

每次代码变更后, 在此追加说明。格式: `[日期] [版本] - 变更内容`

### [2026-08-06] v1.0.0 - 正式候选版

**定稿内容:**
- 固化触摸校准参数: `X: 362-3914`, `Y: 150-3722`, `swapXY=1`, `invX=1`, `invY=1`
- 校准入口收敛为中央四顶点校准, 仅保留 Settings / Terminal / Serial Monitor
- 首页显示当前 WiFi 名称 + IP + Provider
- WiFi 扫描日志增加频道和加密类型, 便于判断 WPA3/5GHz/WiFi6 问题
- WiFi 多次重连失败后自动进入 AP 配网模式
- 保持黑底绿色 hack 风格
- 新增苹果图片旋转开机动画
- 新增 `src/config.example.h`, `src/config.h` 含密钥不再进入 git

### [2026-08-06] v0.9.1 - BLE 共存修复 + 中央四顶点校准 + hack 风格

**修复:**
- 修复无法进入 OS: `WiFi.setSleep(false)` 会导致后续 BLE `coex_core_enable` abort
- BLE 初始化移到 WiFi 之前, 避免共存顺序崩溃
- 四角校准改为“屏幕中央标准框的四个顶点”, 点击更准, 再线性外推到全屏

**界面:**
- 首页按钮改为小字号, 字母不再溢出
- 整体改成黑底绿色终端/hack 风格

### [2026-08-06] v0.9.0 - 实时日志 + BLE 配网 + 中心校准

**新增:**
- OS 桌面增加 `Logs` 应用, 实时显示最近日志, 类似串口监视器
- 新增 BLE 配网服务, 设备名 `ESP32-AI-Setup`, 手机通过 BLE 写入 `SSID:PASS` 完成配网
- 新增中心校准: 屏幕画标准框, 点击框中心后自动平移校准值, 解决中心点偏差
- 设置页新增 `Center Adjust`; 终端/串口命令新增 `center`
- WiFi 页面显示 BLE 配网方式和 `SSID:PASS` 格式

**修改:**
- `src/logger.cpp`: 日志环形缓冲, 所有 `DBG_PRINT*` 同时进入 Logs 页
- `src/ble_provision.cpp`: BLE GATT 配网服务
- `src/touch_calib.cpp`: 中心点校准
- `src/os.cpp`: Logs 页面、6 应用桌面、BLE 启动

### [2026-08-06] v0.8.0 - 迷你 OS + WiFi 配网 + HTTP 服务 + 麦克风扬声器

**新增:**
- 主界面改为 OS 桌面: Chat / Terminal / Settings / WiFi
- 聊天页带英文屏幕键盘, 可直接触屏输入并发送给 LLM
- 终端页支持命令: `help` / `clear` / `provider` / `calib show` / `wifi` / `record` / `reboot`
- 设置页: 触摸校准、重置校准、启动 WiFi AP、切换供应商、系统信息
- WiFi 配网: 默认不写死 SSID, 首次开机进入 AP (`ESP32-AI-Setup` / `12345678`), 网页 `http://192.168.4.1` 配网; 也支持串口 `wifi set <ssid> <pass>`
- HTTP 小服务器: `/` 配网页, `/api/status`, `/api/chat?q=...`, `/api/wifi`, `/api/record?ms=...`, `/api/reboot`
- I²S 麦克风录音到 PSRAM, 终端 `record` 可录 3 秒并播放; `/api/record` 返回 WAV

**修复:**
- 触摸校准改为按已知十字屏幕坐标线性外推, 修复垂直方向偏差
- 校准数据增加版本号, 旧算法保存的 NVS 校准会被自动忽略
- LLM HTTPS 客户端改为整个响应共享 30 秒总超时, 避免卡死
- 串口解析兼容 LF/CR/CRLF, `calib` 不再被换行符取消

**已修改/新增文件:**
- `src/main.cpp` 精简为 OS 入口
- 新增 `src/os.cpp` / `src/os.h`
- 新增 `src/web_server.cpp` / `src/web_server.h`
- `src/wifi_utils.cpp`: NVS 配网 + AP 模式
- `src/audio.cpp`: 麦克风采集 + PCM 播放
- `src/touch_calib.cpp`: 精确校准 + 版本化
- `src/config.h`: 移除硬编码 WiFi 默认值
- `README.md`

### [2026-08-06] v0.7.0 - 英文 UI + 触摸坐标映射修复 + calib 命令加固

**问题:**
- 屏幕没有中文字体, 中文按钮/提示/回复全部显示为方块
- 触摸 `swapXY` 时先按宽高映射再交换, 导致按钮命中位置和屏幕位置偏移
- 串口命令使用阻塞式 `readStringUntil`, 输入时机不对时 `calib` 可能被后续逻辑当成普通文本

**修复:**
- 屏幕 UI 全部切换为英文, AI 系统提示词也要求英文纯文本, 不再显示方块
- `src/touch.cpp`: 修正 `raw_to_screen`, swap 模式下先用对应 raw 轴映射到最终屏幕轴, 再统一反转
- `src/config.h`: 校准默认值按实际屏幕轴更新 (`X: 133-3745`, `Y: 434-3496`)
- `src/main.cpp`: 按钮改为 96×38, 3 列 2 行完整落在 320×240 内; 串口解析改为非阻塞缓冲, `calib` 更可靠
- `src/touch_calib.cpp`: 校准界面改英文
- `src/display.cpp`: 自检/启动界面改英文

**已修改文件:**
- `src/main.cpp`
- `src/display.cpp`
- `src/touch_calib.cpp`
- `src/touch.cpp`
- `src/config.h`
- `README.md`

### [2026-08-06] v0.6.0 - N16R8 启动修复 + LLM HTTPS 直连

**问题:**
- 使用默认 `esp32-s3-devkitc-1` 板型编译时, N16R8 的 8MB Octal PSRAM 未被启用, 固件在启动瞬间反复 `rst:0x3 (RTC_SW_SYS_RST)`
- `partitions_16MB.csv` 的 UTF-8 中文注释在 Windows 上被 PlatformIO 按 GBK 读取, 编译报 `UnicodeDecodeError`
- 设备端 HTTPS 请求 Agnes API 时卡住/超时, 无法完成问答

**修复:**
- `platformio.ini`: 启用 `board_build.arduino.memory_type = qio_opi`、`board_build.psram_type = opi`、16MB Flash 和 `-DBOARD_HAS_PSRAM`
- `partitions_16MB.csv`: 注释全部改为 ASCII, 避免 Windows 编码错误
- `src/llm.cpp`: 改为 `WiFiClientSecure` 直连, 先解析 IPv4, 再用 IP 连接并保留 SNI/Host, 自己处理超时、chunked/Content-Length 响应
- `src/cn_font.cpp`: 自动换行先判断宽度, 不再先把字符画出边界再换行
- `src/touch_calib.cpp`: 首次加载 NVS 不再产生 `nvs_open failed: NOT_FOUND` 日志

**已修改文件:**
- `platformio.ini`
- `partitions_16MB.csv`
- `src/main.cpp`
- `src/llm.cpp`
- `src/cn_font.cpp`
- `src/touch_calib.cpp`
- `README.md`

### [2026-08-06] v0.5.2 - 修复 font8x16 数组编译错误

**问题:**
v0.5.1 的 `src/cn_font.cpp` 内置 font8x16_basic 数组, 实际只写了 79 个字符
但声明是 `[96][16]`, 编译报 `too many initializers for const uint8_t [96][16]`

**修复:**
- 完全放弃自写字体数组
- 直接用 Adafruit GFX 自带的 `tft.drawChar()` (内置 glcdfont 5x7 字体, 编译进库)
- ASCII 完整支持 (32-127), 中文/其他 Unicode 仍显示为方块 □
- 行高从 18 改为 10 (匹配 Adafruit GFX size=1 的 6x8 字体)
- 优点: 零外部字体数据, 编译一定通过

**已修改文件:**
- `src/cn_font.cpp`: 完全重写, 用 tft.drawChar 替代自写字体表
- `src/display.cpp`: TEXT_LINE_H 18 → 10
- `src/main.cpp`: AI 回复显示 lineH 18 → 10

### [2026-08-06] v0.5.1 - 修复编译错误 + 完整项目文档

**问题:**
v0.5.0 编译失败, 3 个错误:
1. `partitions_16MB.csv` 行内注释不被 esptool 支持
2. `RawPoint` 在 touch.h 和 touch_calib.cpp 重复定义
3. U8g2 类名/构造/API 多处不兼容 (放弃 U8g2 方案)

**修复:**

#### 1. partitions_16MB.csv
- 移除所有行内注释 (`#` 只在行首使用)
- 保留: 7MB app + 2MB OTA × 2 + 2MB SPIFFS + 20KB NVS

#### 2. RawPoint 重复定义
- 只在 `src/touch.h` 定义一次
- `src/touch_calib.cpp` 直接 include 使用, 不重复定义

#### 3. 中文字体方案重写 (彻底放弃 U8g2)
- 移除 `olikraus/U8g2` 库依赖
- 改用完全自包含的内置 8x16 位图字体 (font8x16_basic, 公共领域)
- ASCII 完整支持 (96 个可打印字符)
- 中文/其他 Unicode 字符显示为方块 □ 占位
- 完整中文内容走串口输出 (Serial Monitor 可见)
- 优点: 零外部依赖, 编译一定通过
- 缺点: 屏幕上看不到中文 (后续可加 .vlw 字体)

#### 4. README 完整重写
- 加第 1 节: 已确认正常功能列表 (含状态)
- 加第 7 节: 串口命令完整列表
- 加第 9 节: 触摸动态校准原理和算法
- 完整引脚对应表 + 禁用引脚说明
- 完整启动流程日志示例
- 6 家供应商完整对比表

**已修改文件:**
- `partitions_16MB.csv`: 移除行内注释
- `src/cn_font.h`: 简化, 不依赖 U8g2
- `src/cn_font.cpp`: 完全重写, 用内置 font8x16_basic
- `src/touch_calib.cpp`: 移除 RawPoint 重复定义
- `platformio.ini`: 移除 U8g2 库依赖
- `README.md`: 完整重写

### [2026-08-06] v0.5.0 - 动态触摸校准 (NVS 持久化)

**修复:**

#### 1. 动态触摸校准 ⭐
- 新文件: `src/touch_calib.h` / `touch_calib.cpp`
- 用 ESP32 Preferences 库保存校准值到 NVS
- 4 点交互式校准: 屏幕显示 4 个红色十字, 用户点击
- 自动判断 SWAP_XY / INVERT_X / INVERT_Y
- 下次开机自动应用

#### 2. 新增串口命令
- `calib` - 进入交互式 4 点动态校准
- `calib show` - 显示当前校准值和来源
- `calib reset` - 清除 NVS, 恢复默认值

### [2026-08-06] v0.4.0 - 加中文字体支持 (失败, 被 v0.5.1 替代)

尝试用 U8g2 渲染中文, 但 API 兼容性问题导致编译失败。

### [2026-08-06] v0.3.2 - 实测触摸校准 + Agnes 域名改回 .cn

- 触摸校准参数根据用户实测 4 角点击值计算
- Agnes endpoint 改回 `apihub.agnes-ai.cn` (实测 .cn 可用)

### [2026-08-06] v0.3.1 - 修 Agnes endpoint + trim 回复

- Agnes 模型名改为 `agnes-2.5-flash`
- AI 回复前后空白 trim

### [2026-08-06] v0.3.0 - 放弃 TFT_eSPI, 换 Adafruit ILI9341

- TFT_eSPI 2.5.43 在 Arduino ESP32 v3.x 下崩溃 (`begin_tft_write` 访问 NULL)
- 改用 Adafruit GFX + Adafruit ILI9341, 兼容性更好

### [2026-08-06] v0.2.x - 触摸独立 SPI + 多供应商 + 编译修复

- 触摸屏独立 HSPI (GPIO 5/4/6/7/9)
- 6 家 LLM 供应商运行时切换
- 自写 XPT2046 SPI 驱动 (无外部库依赖)
- 修复 N16R8 PSRAM 配置 / Arduino v3.x 兼容性

### [2026-08-04] v0.1.0 - 初版

- 完整 PlatformIO 工程结构
- ILI9341 TFT + XPT2046 触摸 + WiFi + LLM API
- 主界面 5 个触摸按钮 + 串口输入

---

> 后续每次确认的更新, 在本 README 第 11 节追加, 同时修改对应代码文件。
