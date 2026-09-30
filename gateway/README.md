# O'Clock 电脑语音网关

网关接收设备的 24 kHz PCM 录音，调用 Paraformer、DeepSeek 和 CosyVoice，返回可播放的 WAV。设备按 BOOT 录音 5 秒后请求网关；请求失败时回放刚录下的声音。

## 本机启动

使用 Python 3.11：

```powershell
cd gateway
python -m unittest -v
python -m pip install dashscope
```

配置环境变量（PowerShell 示例，值请填入你在控制台新建的密钥）：

```powershell
$env:DASHSCOPE_API_KEY = '<阿里云新密钥>'
$env:DEEPSEEK_API_KEY = '<DeepSeek新密钥>'
$env:DEEPSEEK_MODEL = 'deepseek-chat'
$env:DASHSCOPE_WS_URL = 'wss://<WorkspaceId>.cn-beijing.maas.aliyuncs.com/api-ws/v1/inference'
$env:DASHSCOPE_HTTP_URL = 'https://maas.qianwenaiapi.com/api/v1'
$env:DASHSCOPE_ASR_MODEL = 'paraformer-realtime-8k-v2'
$env:DASHSCOPE_TTS_MODEL = 'cosyvoice-v3-flash'
$env:DASHSCOPE_TTS_VOICE = 'longanyang'
$env:OCLOCK_GATEWAY_TOKEN = '<自定义局域网令牌>'
python server.py --host 0.0.0.0
```

电脑和设备均连接同一个热点时，在电脑上打开 http://&lt;电脑IP&gt;:8765/health 检查服务；Ctrl+C 关闭。热点重新分配地址后，需要同步更新固件的 `main/oclock_secrets.h` 中的 `OCLOCK_VOICE_GATEWAY_URL`（见根目录 README 的「配置」一节）并重新构建、刷机。启动时 Windows 防火墙若提示 Python 网络访问，允许当前专用网络。电脑本机能访问只证明服务已启动，设备还需要实机连通性验证。

## 接口约定

- GET /health：报告当前模式与音频格式。
- POST /audio/echo：提交原始 PCM，返回包含相同采样数据的 WAV。
- POST /audio/ask：提交原始 PCM，返回识别文本、回答文本和 base64 WAV。
- POST /audio/ask + `X-OClock-Audio: raw`：返回设备可直接播放的 WAV。
- 输入格式：24 kHz、单声道、16-bit 小端有符号 PCM。
- Content-Type: application/octet-stream；必须有 Content-Length。
- 最多 10 秒 / 480000 字节；一次处理一段音频。
- 设备当前的 5 秒录音对应 240000 字节。
- ASR 请求会暂存 WAV 于系统临时目录，调用结束后删除；不记录音频正文或密钥。

局域网联调请求携带 `Authorization: Bearer <自定义局域网令牌>`。
这个令牌只用于设备与电脑之间，不是大模型 API Key。
该 HTTP 模式仅用于可信局域网联调，云端部署需使用 HTTPS。

ASR 的 Workspace ID 需要从阿里云控制台填入 `DASHSCOPE_WS_URL`。`cosyvoice-v3.5-plus` 不支持系统音色，只有先创建声音复刻或声音设计音色并取得音色 ID 才能使用。`longanhuan_v3.6` 是 Qwen-Audio-TTS 音色，不能与 CosyVoice 模型搭配。实际云端联调需要凭证和联网验证。
