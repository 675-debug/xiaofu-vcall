# xiaofu-vcall

一个基于 Qt/C++、Linux epoll、WebRTC 和 FunASR 的桌面即时通信项目，包含 Windows 客户端、业务服务器和独立实时字幕服务。

## 架构

```text
Windows Client A ── TCP/JSON ──┐
                               ├── xiaofu_server :9000 ── MySQL
Windows Client B ── TCP/JSON ──┘        │
         ▲                              └── 只转发 WebRTC 信令
         └──── WebRTC P2P / TURN ──────────► Client

Client Remote AudioTrack
  └── AudioWorklet → 16kHz mono PCM16LE
      └── WebSocket → FunASR_Server :10095 → 实时字幕
```

- 音视频不经过业务服务器；优先直连，失败时使用 TURN。
- 字幕由客户端直接连接 FunASR，不经过业务服务器。
- TCP 协议保持 `4 字节大端长度头 + JSON payload + 累积 Buffer 拆帧`。

## 功能

- 登录、注册、找回密码、联系人、好友申请和在线状态。
- 文字聊天、历史记录、删除会话和清空记录。
- 视频呼叫、接听、拒绝、取消和挂断。
- 摄像头、麦克风、清晰度、全屏、录屏和设备热恢复。
- WebRTC Offer/Answer/ICE、STUN/TURN fallback。
- 对端音频实时中文识别和 partial/final 字幕。

## 技术栈

| 模块 | 技术 |
|---|---|
| Client | Qt Widgets、Qt WebEngine、QWebChannel、JavaScript WebRTC |
| 业务 Server | C++17、非阻塞 Socket、epoll ET、线程池、eventfd、MySQL |
| ASR Server | Python、WebSocket、FunASR、Paraformer streaming |

## 目录

```text
client/
├── src/                  Qt 页面、NetworkManager、通话控制器
├── ui/                   Qt Designer .ui
├── resources/video/      通话页面与 WebRTC JavaScript
└── tools/                Client 静态审计

server/xiaofu_server/
├── src/net/              Connection、TcpServer、EpollLoop
├── src/concurrency/      ThreadPool、CompletionDispatcher
├── src/handler/          账号、聊天、联系人、CallSession
├── src/db/               MySQL 数据访问
└── tools/                单元与并发测试

server/FunASR_Server/
├── src/asr_server.py     WebSocket ASR 服务
├── model/                本地模型占位，不提交模型文件
└── logs/                 运行日志占位
```

## Windows Client

Qt 6.11.1 + MSVC2022 构建并生成 ZIP：

```powershell
deploy_windows_qt6.bat
```

发布目录：

```text
dist/xiaofu-vcall-client-win64/
dist/xiaofu-vcall-client-win64.zip
```

常用配置：

```text
XIAOFU_SERVER_HOST       业务服务器地址，默认 8.137.152.134
XIAOFU_SERVER_PORT       默认 9000
XIAOFU_STUN_URL          STUN 地址
XIAOFU_TURN_URL          TURN 地址
XIAOFU_TURN_USERNAME     TURN 用户名
XIAOFU_TURN_CREDENTIAL   TURN 密码
XIAOFU_ICE_POLICY        all / relay
XIAOFU_ASR_URL           FunASR WebSocket 地址
```

FunASR 地址也可放在 exe 同目录的 `asr.url`。环境变量优先于配置文件。

## Linux 业务 Server

```bash
sudo apt update
sudo apt install -y build-essential cmake libmysqlclient-dev libssl-dev

cmake -S server/xiaofu_server \
      -B server/xiaofu_server/build-linux-release \
      -DCMAKE_BUILD_TYPE=Release
cmake --build server/xiaofu_server/build-linux-release -j2
ctest --test-dir server/xiaofu_server/build-linux-release --output-on-failure
```

MySQL 配置：

```text
XIAOFU_MYSQL_HOST       默认 127.0.0.1
XIAOFU_MYSQL_PORT       默认 3306
XIAOFU_MYSQL_USER       默认 xiaofu
XIAOFU_MYSQL_PASSWORD   必须由部署环境提供
XIAOFU_MYSQL_DATABASE   默认 xiaofu
```

生产环境通过 `server/xiaofu_server/deploy/` 中的 systemd 配置运行。MySQL 不应对公网开放 `3306`。

## FunASR Server

当前输入协议：16kHz、mono、PCM16 little-endian；客户端每 60ms 发送 960 samples，服务端累计约 600ms 后执行一次流式推理。

```bash
cd server/FunASR_Server
python3 -m venv venv
source venv/bin/activate
pip install funasr numpy websockets psutil

python src/asr_server.py \
  --host 0.0.0.0 \
  --port 10095 \
  --model-dir /absolute/path/to/paraformer-online-model
```

模型只在启动时加载一次，每个连接维护独立流式状态。详细协议与自测命令见 [`server/FunASR_Server/README.md`](server/FunASR_Server/README.md)。

## 验证

```powershell
client/tools/ui_static_audit.ps1
client/tools/video_call_state_audit.ps1
client/tools/webrtc_page_audit.ps1
```

业务服务器测试位于 `server/xiaofu_server/tools/`；其中数据库相关测试需要可用的 MySQL 测试环境。

## 安全

- 不提交 MySQL、TURN、云服务器或其他生产凭据。
- 公网 FunASR 建议通过反向代理提供 `wss://` 并限制访问来源。
- `model/`、`venv/`、日志、构建目录和发布包均不进入 Git。

## License

[MIT License](LICENSE)
