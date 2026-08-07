# xiaofu-vcall

`xiaofu-vcall` 是一个基于 C++ / Qt 的即时通信与视频通话练习项目，目标是把 Linux 网络编程、多任务编程、SQLite 数据持久化、Qt 桌面 UI、WebRTC/FFmpeg 视频通话这些知识点串成一个能稳定演示的秋招项目。

当前重点已经从静态 UI 进入真实通信链路：服务端支持 TCP 长连接、epoll 事件循环、线程池任务处理、SQLite 历史消息保存；客户端支持账号登录、联系人、中文聊天、好友申请、聊天记录和视频通话页面。

## 当前能力

- Qt Widgets 客户端：登录、注册、找回密码、联系人列表、聊天页面、视频通话页面。
- 云服务器通信：客户端默认连接 `8.137.152.134:9000`，也可以通过环境变量切换测试地址。
- TCP 应用层协议：`4 字节大端长度头 + JSON payload`，解决 TCP 粘包/半包问题。
- 真实文字聊天：两个客户端可以通过服务端在线实时转发中文消息。
- SQLite 历史消息：服务端保存聊天记录，客户端切换联系人时可拉取历史。
- 联系人与好友申请：支持添加联系人、好友申请弹窗、同意后互为联系人。
- 在线状态同步：客户端 join 后通过 heartbeat 保活，服务端推送联系人在线/离线状态。
- Linux 服务端：基于 epoll 边缘触发、非阻塞 socket、线程池、eventfd 回传结果。
- 服务端后台运行：提供 `--daemon` 参数，也提供 systemd 部署文件。
- 视频通话原型：已接入 Qt WebEngine + WebChannel + WebRTC 页面，服务端只负责信令转发，不做视频编解码。

## 项目结构

```text
xiaofu-vcall/
├── client/                         # Qt Widgets 客户端
│   ├── src/                        # 页面组件、NetworkManager、视频控制器
│   ├── src/video/                  # WebRTC 与 Qt WebChannel 桥接
│   ├── ui/                         # Qt Designer UI 文件
│   ├── resources/                  # 图标与视频通话 HTML
│   └── xiaofu-vcall-client.pro
├── server/                         # C++ 服务端
│   ├── src/net/                    # SocketPlatform、Connection、TcpServer、EpollLoop
│   ├── src/concurrency/            # ThreadPool、CompletionDispatcher
│   ├── src/handler/                # 注册、登录、聊天、联系人、信令处理
│   ├── src/db/                     # SQLite、用户、好友、聊天记录、密码哈希
│   ├── src/process/                # daemon 支持
│   ├── deploy/                     # Ubuntu/systemd 部署文件
│   └── CMakeLists.txt
├── tools/                          # 集成测试与辅助脚本
├── docs/video-call-technical-plan.md
└── README.md
```

## 通信架构

客户端页面不直接操作 socket，而是统一通过 `NetworkManager` 发送请求。服务端接收 TCP 数据后先按长度头拆包，再根据 JSON 的 `type` 分发到对应业务处理器。

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
  +--> WebRTC 通话信令转发
  |
  v
SQLite
```

主要协议类型包括：

```text
register, login, forgot, join, heartbeat,
add_contact, friend_request, friend_accept, contacts, presence_push,
chat, chat_push, history, delete_chat, clear_chats,
call_request, call_accept, call_reject, call_hangup,
webrtc_offer, webrtc_answer, webrtc_ice
```

## 服务端技术点

- `epoll_create1` 创建 epoll 实例。
- `epoll_ctl` 注册监听 socket、客户端 socket、eventfd。
- `epoll_wait` 等待网络事件和工作线程完成事件。
- 使用边缘触发 `EPOLLET`，socket 设置为非阻塞。
- accept/recv/send 循环读写到 `EAGAIN`，避免边缘触发漏事件。
- 线程池使用 C++ 标准库 `std::thread`、`std::mutex`、`std::condition_variable`。
- I/O 线程只负责 socket 和在线用户表，数据库操作丢给工作线程。
- SQLite 使用 WAL、busy timeout，降低多线程读写锁冲突。
- 服务端只做消息路由和信令转发，不参与视频编码、解码和转码。

## 客户端技术点

- Qt Widgets 实现主界面、聊天页、联系人列表和视频通话窗口。
- `QTcpSocket` 维护长连接，统一处理连接、断线、拆包、发包。
- `QJsonObject` 组装业务请求，协议体保持清晰可读。
- `QWebEngineView` 承载 WebRTC 页面。
- `QWebChannel` 在 C++ 和 JavaScript 之间传递通话信令。
- 视频通话按钮只负责 UI 状态，真实媒体流由 WebRTC 负责采集和传输。

## 构建服务端

Ubuntu 22.04 推荐环境：

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config sqlite3 libsqlite3-dev libssl-dev

cd ~/workspace/server
mkdir -p build-linux-epoll
cd build-linux-epoll

cmake ..
cmake --build . -j"$(nproc)"
```

启动服务端：

```bash
mkdir -p /root/xiaofu-vcall-data

./xiaofu-server \
  --host 0.0.0.0 \
  --port 9000 \
  --workers 2 \
  --data-dir /root/xiaofu-vcall-data
```

检查端口：

```bash
ss -lntp | grep 9000
```

## 构建客户端

Windows 推荐使用 Qt 5.12.12 MSVC2017 64-bit：

```powershell
cd D:\MyFile\code\work\workspace\xiaofu-vcall\client
qmake xiaofu-vcall-client.pro
nmake
.\debug\xiaofu-vcall-client.exe
```

Linux 虚拟机需要安装 Qt WebEngine：

```bash
sudo apt update
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

cd ~/xiaofu_vcall/client
qmake xiaofu-vcall-client.pro
make -j"$(nproc)"
```

客户端默认连接云服务器：

```text
8.137.152.134:9000
```

如果需要临时切换服务器地址：

```powershell
$env:XIAOFU_SERVER_HOST="10.7.154.88"
$env:XIAOFU_SERVER_PORT="9000"
.\debug\xiaofu-vcall-client.exe
```

```bash
XIAOFU_SERVER_HOST=127.0.0.1 XIAOFU_SERVER_PORT=9000 ./debug/xiaofu-vcall-client
```

## 视频通话路线

当前视频通话采用轻量方案：

```text
Qt 通话窗口
  |
  v
QWebEngineView
  |
  v
WebRTC getUserMedia / RTCPeerConnection
  |
  +--> 摄像头采集、编码、传输、解码、渲染由客户端完成
  |
  +--> offer/answer/ice 信令通过 TCP 服务端转发
```

FFmpeg 暂时不放进服务端，也不做复杂媒体服务器。后续 FFmpeg 主要用于客户端侧的学习和扩展，例如摄像头设备枚举、本地录制、截图、格式分析，或者作为 WebRTC 方案之外的实验分支。

详细设计见：

[docs/video-call-technical-plan.md](docs/video-call-technical-plan.md)

## 测试建议

- 启动云服务器 `xiaofu-server`。
- Windows 客户端登录账号 A。
- Linux/虚拟机客户端登录账号 B。
- A/B 互加好友，测试联系人在线状态。
- A 给 B 发送中文消息，检查实时接收。
- 断开 B 后，A 发送消息，重新登录 B 后拉取历史。
- 从联系人右键或通话按钮发起视频通话，检查 call 信令日志。

## 后续计划

- 完善 WebRTC 通话弹窗：呼叫、接听、拒绝、挂断。
- 稳定处理摄像头不存在、权限拒绝、虚拟机无摄像头等情况。
- 优化 WebRTC 视频窗口尺寸，避免花屏和比例异常。
- 增加 TURN/coturn 配置，解决复杂 NAT 下无法直连的问题。
- 用 FFmpeg 做客户端本地录制和设备调试辅助。
- 写一份完整项目答辩稿，覆盖 Linux 网络编程、多任务、协议设计、数据库和视频通话。
