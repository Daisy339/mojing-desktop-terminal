# 墨镜桌面智能终端 · Mojing Desktop Terminal

> 一台放在桌上的常显信息终端 —— 用一块**全反射屏**承载时间、天气、室内环境、待办与相册，用**一颗按键**完成全部本地交互，用**浏览器与伴侣 App**完成零学习成本的配置，并用**大模型语音链路**把它从"能看的设备"变成"能问的助手"。

工程内部代号为 **O'Clock**，因此源码中保留了 `oclock_*` 模块前缀、配网热点名 `OClock-Terminal-Setup` 等既有标识符。

---

## 目录

- [项目简介](#项目简介)
- [功能特性](#功能特性)
- [硬件平台](#硬件平台)
- [系统架构](#系统架构)
- [仓库结构](#仓库结构)
- [快速开始](#快速开始)
  - [1. 固件（ESP32-S3）](#1-固件esp32-s3)
  - [2. AI 语音网关](#2-ai-语音网关)
  - [3. 伴侣 App](#3-伴侣-app)
- [配置说明](#配置说明)
- [接口说明](#接口说明)
- [AI 语音链路](#ai-语音链路)
- [自动化测试](#自动化测试)
- [路线图](#路线图)
- [安全与隐私](#安全与隐私)
- [文档](#文档)
- [第三方组件与许可](#第三方组件与许可)

---

## 项目简介

桌面上真正需要反复查看的信息只有四类：**现在几点、外面什么天气、房间舒不舒服、今天还要做什么**。手机能做到，但代价是持续的分心与耗电；传统时钟太单薄；智能音箱没有屏幕；墨水屏刷新太慢。

墨镜桌面智能终端把这件事拆成两条设计原则：

1. **信息要"常显"而不是"推送"** —— 终端不主动打扰用户，抬头即可读到数据，页面以固定节奏自动轮播。
2. **核心价值必须离线成立** —— 时间、日期、室内温湿度、待办与相册不依赖任何电脑或云服务；AI 语音是可选增强，断网时设备依然是完整的时钟与信息屏，而不是"变砖"。

---

## 功能特性

| 模块 | 能力 |
|---|---|
| **时钟主页** | 超大字号时:分 + 秒、日期星期、当前城市；左侧室内温度与湿度；右侧天气温度、动态天气图标与文字摘要；底部两行未完成待办 |
| **天气页** | 当前实况（温度 / 湿度 / 天气摘要）+ "昨天 / 今天 / 明天 / 后天"四日趋势折线（最高温与最低温双线带数值标注）；数据 30 分钟自动刷新，失败按 5 分钟退避重试，断网时继续展示最近一次成功结果并标注陈旧状态 |
| **待办页** | 未完成 / 已完成统计；卡片列表含复选框、标题、截止日期、优先级；逾期项直接以 `OVERDUE` 高亮；排序规则为「未完成 → 有截止日期 → 日期更早 → 优先级更高」 |
| **相册页** | 支持上传图片或 GIF，最多 8 帧、每帧 400×300 单色（约 15 KB）；帧间隔可设 200–5000 ms；空闲轮播时会播放动画 |
| **AI 语音问答** | 按一下 `BOOT` 键录音 5 秒，端到端完成「语音识别 → 大模型回答 → 语音合成 → 扬声器播放」，再按一次可随时取消 |
| **板载 Web 控制台** | 设备自身即网站：配网、改天气城市、增删改待办、上传相册、查看状态，无需安装任何软件 |
| **伴侣 App** | Flutter 编写的 Windows / Android 客户端，复用设备控制台接口，提供图形化的设备设置、待办管理与相册上传 |
| **配网与恢复** | 无有效凭据时自动进入 AP 配网；STA 连接失败 15 秒后自动回退；已联网时也可主动切回配网模式 |
| **离线自持** | 时间由 NTP 校准、PCF85063 RTC 续走；配置与待办存 NVS，相册帧存 SPIFFS，断电不丢 |

### 设备端交互一览

| 操作 | 触发条件 | 行为 |
|---|---|---|
| 短按 `KEY` | 按下至松开 < 1000 ms | 切换下一页（主页 → 天气 → 待办 → 相册） |
| 长按 `KEY` | 按住 ≥ 1000 ms | 触发一次数据刷新，屏幕显示 3 秒刷新反馈 |
| 短按 `BOOT` | 按下即触发 | 开始语音录音；录音/播放中再按则取消 |
| 空闲 | 无操作 12 秒 | 在主页 / 待办 / 相册之间自动轮播（天气页为仅手动页面） |
| 手动操作后 | — | 自动轮播暂停 30 秒，避免"刚翻到就被切走" |

---

## 硬件平台

目标板：[**Waveshare ESP32-S3-RLCD-4.2**](https://docs.waveshare.net/ESP32-S3-RLCD-4.2)（SKU 33298）

| 部件 | 型号 / 规格 | 作用 |
|---|---|---|
| 主控模组 | ESP32-S3-WROOM-1-N16R8 · Xtensa 双核 LX7 @240 MHz · 512 KB SRAM · **16 MB Flash + 8 MB PSRAM** | 承载固件、Wi-Fi 协议栈、HTTP 服务与图形渲染 |
| 显示屏 | 4.2 英寸全反射液晶（RLCD，ST7305），400×300，无需背光 | 常显信息载体，低功耗、无烧屏 |
| 温湿度 | SHTC3（I²C `0x70`） | 桌面真实室内温湿度 |
| 实时时钟 | PCF85063（I²C） | 断网与重启后维持时间连续 |
| 音频 | ES8311 编解码 + ES7210 ADC（回声消除）+ 双麦克风阵列 + 扬声器接口 | 语音输入与回放 |
| 供电 | 18650 锂电池座 + 充放电管理 + RTC 备用电池座 | 脱离线缆独立摆放 |
| 交互 | `KEY` 按键（GPIO18）、`BOOT` 按键（GPIO0） | 切页 / 刷新 / 语音触发 |
| 扩展 | Micro SD 卡槽、2×8 排针、USB、UART、I²C | 后续功能扩展 |

---

## 系统架构

作品采用**端—边—云三层协同**：设备端负责"可靠"，电脑侧网关负责"连接"，云端负责"智能"。任何一层缺失，其余功能仍可独立工作。

```text
[ 云端 ]   Paraformer 语音识别 │ DeepSeek 大语言模型 │ CosyVoice 语音合成 │ 天气与地理编码
                                    ▲
                                    │ HTTPS
                                    ▼
[ 电脑 ]   gateway/server.py :8765   音频重采样 · 三阶段 AI 编排 · 单请求闸门 · 令牌鉴权
                                    ▲
                                    │ HTTP（24 kHz PCM / WAV）
                                    ▼
[ 终端 ]   ESP32-S3 固件（离线自持）
           页面调度 · 状态与视图 · 渲染管线 · NVS/SPIFFS · STA/AP · HTTP 控制台
                                    ▲
                                    ▼
[ 硬件 ]   RLCD 400×300 · KEY/BOOT · SHTC3 · PCF85063 · 麦克风 / 扬声器
```

### 固件分层

固件按"深模块"思想拆分，硬件差异收敛在板级与端口层，业务逻辑可脱离硬件推理与测试：

| 模块 | 职责 |
|---|---|
| `oclock_board.*` | 板级抽象：按键 GPIO、单调时钟、按键事件分发 |
| `oclock_button.*` | 消抖与长短按判定状态机 |
| `oclock_i2c_bus.*` | 统一 I²C 总线与设备句柄 |
| `oclock_rlcd_port.*` | RLCD 端口层：SPI、TE 同步、像素写入与整帧呈现 |
| `oclock_renderer.*` | 把应用状态合成为视图模型并绘制到 1-bit 虚拟画布 |
| `oclock_pages.*` / `oclock_page_scheduler.*` | 页面定义与调度：轮播、手动切换、暂停、刷新反馈 |
| `oclock_time.*` / `oclock_time_view.*` | RTC 读取与校验、NTP 写回、日期星期与文本生成 |
| `oclock_environment.*` | SHTC3 采样（含 CRC 校验）、舒适度判断 |
| `oclock_weather.*` | 城市地理编码、天气抓取解析、缓存兜底、退避重试 |
| `oclock_todo.*` | 待办仓库：增删改查、完成/恢复、排序、逾期判定 |
| `oclock_album.*` | 1-bit 帧持久化、索引、播放节拍推进 |
| `oclock_audio.*` | 音频编解码与 I²S、录音与回放、语音请求状态机 |
| `oclock_config.*` | 配置持久化（NVS） |
| `oclock_network.*` | STA/AP 模式管理、SNTP 校时、状态汇聚 |
| `oclock_web.*` | 板载 HTTP 控制台：页面渲染、表单处理、状态 JSON、相册上传 |
| `oclock_app.*` | 应用主循环：周期调度、脏标记合成与渲染触发 |

### 渲染策略

1. 用 **U8g2** 在内存中建立 400×300 的 1-bit 虚拟画布；
2. 绘制完成后逐像素搬运到 RLCD 端口层，由端口层完成 SPI 传输与整帧刷新（利用 TE 信号同步避免撕裂）；
3. 字体分工：时钟数字用 `logisoso42` 系列作视觉锚点，中文用 GB2312 点阵字库（`wqy14_gb2312b`），小号信息用 6×13 / 8×13 / 10×20 字体；
4. **事件驱动渲染**：主循环 20 ms 周期，但只在语义变化时重新组帧 —— 主页按秒、其他页面按分钟，待办 / 环境 / 相册 / 天气 / 音频 / 网络各自维护版本号置脏。

> 取舍说明：单色反射屏的局部刷新收益有限却显著提高状态管理复杂度，因此本项目选择「**降低刷新频率**」而非「拆分刷新区域」来平衡显示与功耗。

---

## 仓库结构

```text
.
├── o'clock/                     # ESP32-S3 固件（ESP-IDF 工程）
│   ├── main/                    # 全部应用源码与板级配置
│   │   ├── oclock_board_config.h        # 引脚 / 屏幕 / 时区等硬件配置
│   │   ├── oclock_secrets.h.example     # 凭据模板（复制为 oclock_secrets.h）
│   │   └── oclock_*.c / *.cpp / *.h     # 16 个功能模块
│   ├── partitions.csv           # nvs 24 KB · app 1.5 MB · SPIFFS 4 MB
│   ├── sdkconfig.defaults       # esp32s3 / 16MB Flash / 8MB PSRAM / 1kHz tick
│   └── CMakeLists.txt
├── gateway/                     # 电脑侧 AI 语音网关（Python）
│   ├── server.py
│   └── test_server.py
├── app/                         # Flutter 伴侣 App（Windows + Android）
│   ├── lib/                     # main.dart / device.dart / album.dart
│   ├── test/                    # 单元与组件测试
│   └── tooling/                 # 构建辅助脚本
├── docs/                        # 工程文档（含需求与议题跟踪约定）
├── .scratch/oclock-terminal/    # 产品需求文档（PRD）与 10 项实现议题
├── .vendor/README.md            # 第三方板级资料说明（资料本身不入库）
├── .gitignore
└── 墨镜桌面智能终端-作品说明文档.pdf   # 完整作品说明文档（19 页）
```

---

## 快速开始

### 环境要求

| 目标 | 需要安装 |
|---|---|
| 固件 | [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) **≥ 5.3**（含 `idf.py`、Ninja、CMake） |
| 语音网关 | Python **3.11** + `dashscope` 依赖包 |
| 伴侣 App | Flutter SDK（Dart ≥ 3.4）；Windows 需 Visual Studio 的 *Desktop development with C++*；Android 需 Android Studio 与 SDK |

### 1. 固件（ESP32-S3）

**① 补齐第三方板级资料**

构建依赖微雪官方资料包中的 U8g2 与 SensorLib，该资料**不随仓库分发**。请按 [`.vendor/README.md`](.vendor/README.md) 下载并解压到 `.vendor/waveshare-esp32-s3-rlcd-4.2/`。

**② 生成本地凭据文件**

```bash
cp o'clock/main/oclock_secrets.h.example o'clock/main/oclock_secrets.h
# 然后编辑 oclock_secrets.h 填入你自己的 Wi-Fi 与语音网关信息
```

> `oclock_secrets.h` 已在 `.gitignore` 中，**不要提交**。若暂时留空，固件仍可编译，设备会直接进入 AP 配网模式。

**③ 构建与烧录**

```bash
cd o'clock
idf.py set-target esp32s3        # 首次或更换芯片型号时执行
idf.py build
idf.py -p <串口> flash monitor
```

> 也可使用工程自带的 Dev Container（`.devcontainer/`，基于 `espressif/idf` 镜像）获得一致的构建环境。

**④ 首次配网**

1. 连接热点 `OClock-Terminal-Setup`（密码 `12345678`）；
2. 浏览器打开 `http://192.168.4.1`；
3. 在「网络配置」填写 Wi-Fi 名称与密码并保存，设备将切换网络并自动校时。

### 2. AI 语音网关

网关在**与设备同一局域网的电脑**上运行，负责持有云端凭据并编排三阶段 AI 调用。

```bash
cd gateway
python -m pip install dashscope
python -m unittest -v            # 可选：先跑一遍测试
```

配置环境变量（PowerShell 示例，值请填入你自己的密钥）：

```powershell
$env:DASHSCOPE_API_KEY     = '<你的阿里云 DashScope 密钥>'
$env:DEEPSEEK_API_KEY      = '<你的 DeepSeek 密钥>'
$env:DEEPSEEK_MODEL        = 'deepseek-chat'
$env:DASHSCOPE_WS_URL      = 'wss://<WorkspaceId>.cn-beijing.maas.aliyuncs.com/api-ws/v1/inference'
$env:DASHSCOPE_HTTP_URL    = 'https://maas.qianwenaiapi.com/api/v1'
$env:DASHSCOPE_ASR_MODEL   = 'paraformer-realtime-8k-v2'
$env:DASHSCOPE_TTS_MODEL   = 'cosyvoice-v3-flash'
$env:DASHSCOPE_TTS_VOICE   = 'longanyang'
$env:OCLOCK_GATEWAY_TOKEN  = '<自定义局域网令牌，需与固件一致>'

python server.py --host 0.0.0.0
```

自检：浏览器访问 `http://<电脑IP>:8765/health`，返回内容中应显示 `"mode": "cloud-voice"`。

> 安全提示：`server.py` 在监听非回环地址时**强制要求**设置 `OCLOCK_GATEWAY_TOKEN`，否则拒绝启动。该 HTTP 模式仅适用于可信局域网；若要部署到公网，请改用 HTTPS。

### 3. 伴侣 App

在 `app/` 目录下执行：

```bash
flutter pub get
flutter analyze
flutter test
flutter run -d windows           # 或 flutter run -d <Android 设备ID>
```

产物构建：

```bash
flutter build windows            # 发布时需保留整个 Release 目录，不能只拷贝 exe
flutter build apk --release      # 产物：app/build/app/outputs/flutter-apk/app-release.apk
```

Windows 平台构建有两点注意：

- Windows 插件需要符号链接权限，如遇相关报错请在系统设置中开启**开发者模式**；
- Flutter 的 Windows 构建不接受父目录名中的英文撇号，可使用 `subst` 建立盘符别名（文件不会复制或搬移）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tooling\flutter-safe.ps1 run -d windows
```

该脚本会校验并复用已有的盘符映射，不会覆盖其他映射。

---

## 配置说明

所有硬件相关的可调项集中在 **`o'clock/main/oclock_board_config.h`**（屏幕尺寸、各路 GPIO、NTP 服务器、时区、按键消抖与长按阈值）；与个人环境相关的凭据集中在 **`o'clock/main/oclock_secrets.h`**（不纳入版本控制）：

```c
#define OCLOCK_WIFI_SSID           "your-wifi-ssid"
#define OCLOCK_WIFI_PASSWORD       "your-wifi-password"
#define OCLOCK_VOICE_GATEWAY_URL   "http://192.168.1.100:8765/audio/ask"
#define OCLOCK_VOICE_GATEWAY_TOKEN "change-me"
```

| 关键参数 | 取值 |
|---|---|
| 屏幕 | 400×300（横屏逻辑分辨率） |
| 按键 | 40 ms 消抖；≥1000 ms 判定长按 |
| 页面轮播 / 手动暂停 | 12 s / 30 s |
| 待办 | 上限 16 条，标题 64 字节，设备默认展示 5 条以内 |
| 相册 | 最多 8 帧，400×300 单色，帧间隔 200–5000 ms |
| 天气刷新 | 正常 30 min；失败 5 min 退避重试 |
| 配网热点 | `OClock-Terminal-Setup`，WPA2，AP 地址 `192.168.4.1` |
| STA 回退 / 重试 | 15 s 未连上回退 AP；STA 重试 3 次；AP 重试间隔 10 s |
| 语音采样与时长 | 24 kHz 单声道 16-bit，单次录音 5 s（上限 10 s） |

---

## 接口说明

### 设备端 HTTP 接口（板载 HTTP 服务，默认端口 80）

| 方法 | 路径 | 作用 |
|---|---|---|
| `GET` | `/` | 完整管理控制台页面（HTML + 内联样式与脚本） |
| `GET` | `/status` | 设备状态 JSON：网络模式、IP、已连接网络、AP 名称、校时状态、待办统计 |
| `POST` | `/wifi` | 保存 Wi-Fi 凭据并切换网络（`ssid`、`password`） |
| `POST` | `/ap` | 主动进入 AP 配网模式 |
| `POST` | `/weather` | 设置天气城市并触发地理编码（`city`） |
| `POST` | `/todos/create` | 新建待办（`title`、`due_date`、`priority`） |
| `POST` | `/todos/update` | 编辑待办（`id` + 上述字段） |
| `POST` | `/todos/complete` | 标记完成或恢复（`id`、`completed`） |
| `POST` | `/todos/delete` | 删除待办（`id`） |
| `POST` | `/album/upload` | 上传相册帧（JSON：`width`、`height`、`interval_ms`、`frames` 十六进制数组） |
| `POST` | `/album/clear` | 清空相册 |

> 设计说明：设备端 **不提供身份认证**，控制台面向可信局域网使用。相册的图像缩放、灰度化、二值化与位打包全部在浏览器或 App 侧完成，设备只接收最终可显示的 1-bit 帧，从而避免在 MCU 上解码 GIF。

### 语音网关接口（局域网 HTTP 服务，默认端口 8765）

| 方法 | 路径 | 作用 |
|---|---|---|
| `GET` | `/health` | 健康检查：当前模式（`audio-echo` / `cloud-voice`）、音频格式、云端配置齐备情况 |
| `POST` | `/audio/echo` | 回声测试：把收到的 PCM 原样打包为 WAV 返回 |
| `POST` | `/audio/ask` | 语音问答：返回识别文本、模型回答与合成语音；带 `X-OClock-Audio: raw` 头时直接返回可播放的 WAV |

**约定**：输入为 24 kHz、单声道、16-bit 小端 PCM，`Content-Type: application/octet-stream`，必须携带 `Content-Length`；单次音频上限 10 秒（480000 字节）。局域网调用需携带 `Authorization: Bearer <令牌>`；同一时刻只处理一路音频，其余请求返回 `503 busy`。

---

## AI 语音链路

设备端只做"入口与出口"，计算全部交给网关与云端，因此**更换模型只需改网关，无需重新烧录固件**。

```text
按下 BOOT
   └─ 麦克风录音 5 s @24 kHz 单声道 16-bit
        └─ HTTP POST 原始 PCM（携带令牌）
             └─ 网关重采样至 8 kHz
                  └─ ① Paraformer 语音识别  → 文本
                       └─ ② DeepSeek 大语言模型 → 回答
                            └─ ③ CosyVoice 语音合成 → PCM 22050 → WAV
                                 └─ 返回设备 → 扬声器播放
```

| 环节 | 选型 | 设计要点 |
|---|---|---|
| 语音识别 | `paraformer-realtime-8k-v2` | 设备端保持原生 24 kHz 采集不变，由网关统一重采样到 8 kHz，设备不为云端模型的格式要求买单；识别为空时直接返回错误码，不进入后续调用 |
| 语言模型 | `deepseek-chat` | 系统提示词把角色固定为桌面终端助手，要求**用简洁中文完整回答，通常不超过 150 字** —— 回答是要被"听"的，过长会拖慢合成与播放；限制 512 输出 token、30 秒超时 |
| 语音合成 | `cosyvoice-v3-flash`（音色 `longanyang`） | 合成结果封装为标准 WAV；设备侧提供"原始音频直通"通道，一次请求即拿到可直接播放的音频，无需在 MCU 上做 Base64 解码 |

**端侧工程化**：音频模块以「空闲 / 准备 / 录音 / 播放 / 完成 / 已取消 / 错误」七态对外暴露状态与进度；录音缓冲放在 PSRAM；分块采集避免阻塞主循环；音频初始化失败只降级、不阻断启动；云端不可用时回放刚录下的声音，让用户明确感知"没有连上"，而不是静默无响应。

---

## 自动化测试

```bash
# 语音网关
cd gateway && python -m unittest -v

# 伴侣 App（设备地址归一化、控制台 HTML 解析、界面连通性）
cd app && flutter test
```

需求与实现议题记录在 `.scratch/oclock-terminal/`：`PRD.md` 为产品需求文档（用户故事、实现决策、测试决策、明确不做清单），`issues/01`–`issues/10` 为十项实现议题（板级点亮与页面骨架、时间系统、网络配网、环境感知、待办闭环、天气与缓存、轮播与刷新编排、演示打磨、相册上传与帧播放、电脑状态推送）。

---

## 路线图

| 阶段 | 状态 | 范围 |
|---|---|---|
| **P0** | ✅ 已实现 | 时钟 / 天气 / 室内环境 / 待办 / 相册、AP 配网与 STA 连接、板载 Web 控制台、Windows 与 Android 伴侣 App、AI 语音问答闭环 |
| **P1** | 🚧 增强中 | 更丰富的相册播放策略与主题、更多信息页样式、温和的提醒呈现 |
| **P2** | 📋 规划中 | 电脑状态（CPU / 内存）推送上屏、蓝牙配网、更省电的运行模式 |
| **P3** | 📋 规划中 | 多设备统一管理、模板化内容下发、外壳与结构件、小批量试产 |

**明确不做**：触摸交互与触摸优先的界面、设备端本地编辑待办、多按键菜单体系、设备端通用 GIF 解码、账号体系与公网管理后台、P0 阶段的 MQTT 架构、小时级气象曲线、长期环境历史曲线、待办提醒闹钟、多用户协作、深度睡眠等高阶省电编排。

---

## 安全与隐私

本仓库在发布前已做凭据脱敏，请在使用时继续保持这些习惯：

- **凭据不入库**：Wi-Fi 密码、语音网关地址与令牌存放在 `o'clock/main/oclock_secrets.h`，该文件已被 `.gitignore` 排除；仓库只保留不含真实值的 `oclock_secrets.h.example`。
- **云端密钥只从环境变量读取**：`gateway/server.py` 不接受任何硬编码密钥；错误信息在输出前会抹除密钥片段与常见密钥格式（`sk-*`），避免日志泄露。
- **音频即用即删**：识别所需的临时 WAV 写入系统临时目录，调用结束即销毁；服务端不记录音频正文，也不记录对话文本，并主动关闭默认访问日志。
- **网关令牌鉴权**：设备与网关之间使用常量时间比较校验令牌；监听非回环地址时强制要求设置令牌，防止误暴露到局域网之外。
- **设备控制台定位**：板载 Web 控制台面向**可信局域网**设计，不含账号体系。若需跨网络访问，请自行叠加反向代理、HTTPS 与访问控制。

> 如果你 fork 本仓库并自行部署，请务必替换全部示例值，并检查自己的提交中未混入真实凭据。

---

## 文档

- 📄 **[墨镜桌面智能终端 · 作品说明文档（PDF，19 页）](墨镜桌面智能终端-作品说明文档.pdf)** —— 项目背景、痛点分析、需求分析、目标用户、开发工具、技术方案、功能逐项说明、AI 核心作用、完整使用说明、应用前景与商业模式
- 📋 **[产品需求文档（PRD）](.scratch/oclock-terminal/PRD.md)** —— 用户故事、实现决策、测试决策与范围边界
- 🧩 **[第三方板级资料说明](.vendor/README.md)** —— 构建前如何补齐 `.vendor`
- 🔧 **[固件工程说明](o'clock/README.md)**、**[语音网关说明](gateway/README.md)**、**[伴侣 App 说明](app/README.md)**

---

## 第三方组件与许可

本仓库**不包含**任何第三方 SDK 或密钥，但构建与运行依赖下列组件，它们各自遵循其原有许可：

| 组件 | 用途 | 许可 |
|---|---|---|
| [ESP-IDF](https://github.com/espressif/esp-idf) | 固件框架（FreeRTOS / Wi-Fi / LWIP / NVS / SPIFFS / HTTP） | Apache-2.0 |
| [U8g2](https://github.com/olikraus/u8g2) | 单色图形库与字库 | GPL-2.0（含例外条款，商用前请自行确认） |
| [esp_codec_dev](https://components.espressif.com/components/espressif/esp_codec_dev) | 音频编解码设备抽象 | Apache-2.0 |
| Waveshare 板级资料 / SensorLib | SHTC3 与 PCF85063 驱动参考 | 见原始发行物 |
| [Flutter](https://flutter.dev) / Dart | 伴侣 App | BSD-3-Clause |
| [DashScope SDK](https://pypi.org/project/dashscope/) | 语音识别与语音合成调用 | Apache-2.0 |

> ⚠️ **许可状态说明**：本作品目前**尚未附加开源许可证**。在未明确声明许可前，默认保留全部权利。若你计划将本仓库用于商用或再分发，请先确认 U8g2（GPL-2.0）等组件的许可义务，并为本项目补充合适的许可证文件。

---

<p align="center"><sub>墨镜桌面智能终端 · Mojing Desktop Terminal · 工程代号 O'Clock</sub></p>
