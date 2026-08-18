# FunASR_Server（独立实时字幕服务）

这是与业务通信服务 `../xiaofu_server/` 分离部署的实时语音识别服务。

## 当前职责与数据流

- 本服务只做 ASR 推理与字幕输出，不处理登录、好友、聊天、CallSession、WebRTC 信令或 TURN。
- 当前链路是 `Client → WebSocket → FunASR_Server → Client`。
- `xiaofu_server` 不转发 PCM，也不参与当前字幕链路。经业务服务转发字幕属于未实现的未来方案。

客户端从 WebRTC 远端 AudioTrack 采集音频，经 AudioWorklet 转换为 16kHz、mono、PCM16 little-endian。每 60ms 发送 960 samples（1920 bytes）。服务端每累计 600ms 调用一次 Paraformer 流式模型。

连接建立后，客户端先发送 `mode=online` 的 JSON handshake，随后发送二进制 PCM，结束时发送 `is_speaking=false` 和 `is_end=true`。服务端返回：

```json
{"type":"partial","text":"..."}
{"type":"final","text":"..."}
```

## 运行

依赖 Python 3、`funasr`、`numpy`、`websockets`、`psutil`。模型在进程启动时加载一次；不同连接只维护各自的流式 cache，共享模型调用会串行执行。

```bash
python3 -m venv venv
source venv/bin/activate
pip install funasr numpy websockets psutil
python src/asr_server.py --host 0.0.0.0 --port 10095 --model-dir /absolute/model/path
```

也可使用 `FUNASR_MODEL_DIR`、`FUNASR_ASR_HOST`、`FUNASR_ASR_PORT`、`FUNASR_MAX_CONNECTIONS` 和 `FUNASR_MAX_PCM_SAMPLES` 环境变量。客户端通过 `XIAOFU_ASR_URL=ws://服务器地址:10095` 注入服务地址。

公网部署应使用防火墙限制来源；需要 TLS 时，可在反向代理层提供 `wss://`。

## Ubuntu systemd 部署

环境变量示例位于 `deploy/funasr-server.env.example`，部署时复制到 `/etc/xiaofu-asr.env` 并按实际模型路径调整。service unit 位于 `deploy/xiaofu-asr.service`。

```bash
sudo install -o root -g root -m 600 deploy/funasr-server.env.example /etc/xiaofu-asr.env
sudo install -o root -g root -m 644 deploy/xiaofu-asr.service /etc/systemd/system/xiaofu-asr.service
sudo systemctl daemon-reload
sudo systemctl enable --now xiaofu-asr.service
sudo systemctl status xiaofu-asr.service
sudo journalctl -u xiaofu-asr.service -f
```

## 最小 WebSocket 检查

下面只检查握手与空会话 final，不验证识别质量：

```bash
python - <<'PY'
import asyncio, json, websockets

async def main():
    async with websockets.connect("ws://127.0.0.1:10095") as ws:
        await ws.send(json.dumps({"mode": "online", "wav_name": "smoke", "is_speaking": True}))
        print(await ws.recv())
        await ws.send(json.dumps({"is_speaking": False, "is_end": True}))
        print(await ws.recv())

asyncio.run(main())
PY
```

## 目录

- `src/`：服务端源码
- `model/`：模型目录，不提交 Git
- `logs/`：运行日志目录，不提交 Git
- `venv/`：Python 虚拟环境，不提交 Git
- `CMakeLists.txt`：仅供 CLion 浏览 Python 工程，不参与部署构建

## 尚未实现

- VAD、标点恢复和 2pass
- 字幕经 `xiaofu_server` 转发给对端
- GPU/CUDA 专用调度
