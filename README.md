# xiaofu-vcall

## 项目简介

`xiaofu-vcall` 是一个基于 C++ / Qt 的即时通信与视频通话练习项目，将 Linux 网络编程、多任务编程、SQLite 数据持久化、Qt 桌面 UI 与 WebRTC 视频通话串成一个可稳定演示的项目。服务端基于 epoll + 线程池实现 TCP 长连接与信令转发，客户端基于 Qt Widgets + Qt WebEngine 实现 IM 与 WebRTC 双向视频通话，媒体流由 WebRTC 在两端直传/中继，服务端只负责信令转发。

## 当前能力

- Qt Widgets 客户端：登录、注册、找回密码、联系人列表、聊天页面、视频通话页面。
- TCP 长连接协议：`4 字节大端长度头 + JSON payload`，解决粘包/半包问题。
- 真实文字聊天：双端通过服务端在线实时转发中文消息。
- SQLite 历史消息：服务端保存聊天记录，客户端可拉取历史。
- 联系人与好友申请：添加联系人、好友申请弹窗、同意后互为联系人。
- 在线状态同步：heartbeat 保活，服务端推送联系人在线/离线状态。
- 视频通话：Qt WebEngine + WebChannel + WebRTC（RTCPeerConnection），服务端只转发 offer/answer/ICE 信令。
  - 双向视频：Caller/Callee 共用唯一 video transceiver/sender，Answer 保证 sendrecv + msid + ssrc。
  - ICE/TURN：支持环境变量注入 STUN/TURN，支持 relay 中继策略。
  - 摄像头能力：动态档位（流畅 320x240 / 标准 640x480 / 高清 1280x720）、热插拔检测、断开/恢复提示。
  - 录制：MediaRecorder 合成 remote + local PiP 生成 WebM 并触发下载。
  - 全屏：Qt WebEngine FullScreenSupport + JS requestFullscreen，Esc 退出。
- Linux 服务端：epoll 边缘触发、非阻塞 socket、线程池、eventfd、SQLite WAL。
- 服务端后台运行：`--daemon` 参数 + systemd 部署文件。

## 项目架构

```text
xiaofu-vcall/
├── client/                         # Qt Widgets 客户端
│   ├── src/                        # 页面组件、NetworkManager、视频控制器
│   ├── src/video/                  # WebRTC 与 Qt WebChannel 桥接
│   ├── ui/                         # Qt Designer UI 文件
│   ├── resources/video/            # 视频通话 HTML + WebRTC 模块 JS
│   └── xiaofu-vcall-client.pro
├── server/
│   ├── xiaofu_server/              # 阿里云部署：C++ 业务通信服务
│   │   ├── src/net/                # SocketPlatform、Connection、TcpServer、EpollLoop
│   │   ├── src/concurrency/        # ThreadPool、CompletionDispatcher
│   │   ├── src/handler/            # 注册、登录、聊天、联系人、信令处理
│   │   ├── src/db/                 # SQLite、用户、好友、聊天记录、密码哈希
│   │   ├── src/process/            # daemon 支持
│   │   ├── deploy/                 # Ubuntu/systemd 部署文件
│   │   └── CMakeLists.txt
│   └── FunASR_Server/              # 腾讯云部署：FunASR 推理服务（骨架）
├── tools/                          # 集成测试与辅助脚本
└── README.md
```

通信链路：

```text
Qt UI
  |
  v
NetworkManager
  |
  v
4字节长度头 + JSON
  |
  v
TCP 长连接
  |
  v
Linux epoll 服务端
  |
  +--> 注册/登录/好友/聊天/历史消息
  |
  +--> WebRTC 通话信令转发（offer/answer/ICE）
  |
  v
SQLite
```

## 配置环境

### 服务端（Ubuntu 22.04）

编译器：g++（build-essential）；依赖：cmake、pkg-config、sqlite3、libsqlite3-dev、libssl-dev。

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config sqlite3 libsqlite3-dev libssl-dev

cd server/xiaofu_server
mkdir -p build-linux-epoll && cd build-linux-epoll
cmake ..
cmake --build . -j"$(nproc)"

# 启动
./xiaofu-server --host 0.0.0.0 --port 9000 --workers 2 --data-dir /root/xiaofu-vcall-data
```

### 客户端（Windows）

- Qt 版本：Qt 5.12.12 MSVC2017 64-bit（含 Qt WebEngine）
- 编译器：MSVC 2017（vcvarsall x64），构建工具 nmake

```powershell
cd client
qmake xiaofu-vcall-client.pro -spec win32-msvc CONFIG+=release
nmake
```

打包发布（可选，自动 windeployqt + 生成 dist ZIP）：

```powershell
deploy_windows.bat
```

### 客户端（Linux / 虚拟机）

- Qt 版本：Qt 5（需 qtwebengine5-dev）
- 编译器：g++（Qt 默认工具链），构建工具 make

```bash
sudo apt install -y \
  build-essential \
  qtbase5-dev \
  qtbase5-dev-tools \
  qtwebengine5-dev \
  qtwebengine5-dev-tools \
  qtdeclarative5-dev \
  libqt5webchannel5-dev \
  libgl1-mesa-dev \
  libnss3 \
  libxkbcommon-x11-0

cd client
qmake xiaofu-vcall-client.pro
make -j"$(nproc)"
```

### 运行配置（环境变量）

```text
XIAOFU_SERVER_HOST      服务器地址（默认 8.137.152.134）
XIAOFU_SERVER_PORT      服务器端口（默认 9000）
XIAOFU_STUN_URL         STUN 地址（默认 stun:8.137.152.134:3478）
XIAOFU_TURN_URL         TURN 地址（默认 turn:8.137.152.134:3478）
XIAOFU_TURN_USERNAME    TURN 用户名
XIAOFU_TURN_CREDENTIAL  TURN 凭据（通过环境变量注入，不要写入代码或提交记录）
XIAOFU_ICE_POLICY       ICE 策略（all / relay）
XIAOFU_CAMERA_PROBE     摄像头探针调试开关（1 开启）
```

示例：

```powershell
$env:XIAOFU_SERVER_HOST="127.0.0.1"
$env:XIAOFU_SERVER_PORT="9000"
$env:XIAOFU_TURN_URL="turn:8.137.152.134:3478"
$env:XIAOFU_TURN_USERNAME="xiaofu"
$env:XIAOFU_TURN_CREDENTIAL="<你的凭据>"
.\debug\xiaofu-vcall-client.exe
```

```bash
XIAOFU_SERVER_HOST=127.0.0.1 XIAOFU_SERVER_PORT=9000 ./debug/xiaofu-vcall-client
```
