# xiaofu-vcall 视频通话技术文档

本文档用于交接给 Claude Code，目标是在现有 `xiaofu-vcall` 项目上继续实现稳定的视频通话。项目定位是本科生秋招项目，不追求复杂媒体服务器，优先保证架构清晰、通信稳定、代码易懂、演示可靠。

## 1. 当前项目背景

项目已经具备：

- Qt Widgets 客户端，包含登录、注册、联系人、聊天、视频通话 UI。
- TCP 长连接协议：`4 字节大端长度头 + JSON payload`。
- Linux 服务端：epoll 边缘触发、非阻塞 socket、线程池、SQLite。
- 真实聊天：在线实时转发、离线保存、历史拉取。
- 联系人和在线状态：好友关系、好友申请、上线/离线推送。
- WebRTC 原型：客户端通过 `QWebEngineView` 加载 HTML 页面，通过 `QWebChannel` 和 C++ 交换信令。

当前重点：把“视频通话信令 + WebRTC 媒体流”稳定跑通。服务端只做信令转发，不做视频编解码。

## 2. 技术选型原则

### 推荐方案

```text
Qt C++ UI
  |
  v
QWebEngineView
  |
  v
WebRTC JavaScript
  |
  +--> getUserMedia 采集摄像头
  +--> RTCPeerConnection 建立 P2P 或 TURN 中继连接
  +--> 浏览器内核完成 VP8/H.264 编码、RTP 传输、解码、渲染
  |
  v
QWebChannel
  |
  v
Qt C++ NetworkManager
  |
  v
TCP 信令服务器
```

### 为什么这样选

- WebRTC 已经内置音视频采集、编码、网络抖动控制、NAT 穿透、解码渲染。
- Qt 5.12.12 的 `QWebEngineView` 可以承载 WebRTC 页面，适合快速做出稳定演示。
- 服务端继续保持简单：只负责账号、好友、聊天和通话信令转发。
- FFmpeg 不放到服务端，避免 2C2G 云服务器被视频转码压垮。
- FFmpeg 可以作为客户端侧扩展能力，用于设备调试、本地录制、截图和后续简历亮点。

## 3. 不要做的事情

这些事情暂时不要做，容易把项目复杂度拉爆：

- 不要在 server 上做视频编码、解码、转码。
- 不要在 server 上转发原始视频帧。
- 不要自己从零实现 RTP/RTCP。
- 不要手写完整 NAT 穿透。
- 不要同时做多人会议，先只做一对一双向视频。
- 不要一开始就做复杂音频处理，优先跑通视频，再考虑音频。

## 4. 服务端职责

服务端只做三类事情：

1. 账号和好友系统。
2. 聊天消息路由和历史保存。
3. 视频通话信令转发。

视频通话相关信令建议保持 JSON 格式：

```json
{
  "type": "call_request",
  "from": "alice",
  "to": "bob",
  "callId": "alice_bob_1720000000"
}
```

```json
{
  "type": "webrtc_offer",
  "from": "alice",
  "to": "bob",
  "callId": "alice_bob_1720000000",
  "sdp": "..."
}
```

建议支持的信令类型：

```text
call_request      发起通话
call_accept       接听通话
call_reject       拒绝通话
call_hangup       挂断通话
webrtc_offer      WebRTC offer SDP
webrtc_answer     WebRTC answer SDP
webrtc_ice        ICE candidate
```

服务端转发规则：

- 检查 `to` 是否在线。
- 在线则通过在线用户表找到对端 fd，实时转发。
- 不在线则返回失败，例如 `code=3`，客户端提示“对方不在线”。
- 服务端不要解析 SDP 内容，只作为字符串透传。
- 服务端不要保存 ICE candidate。
- 通话状态可以先不入库，只维护在线实时状态即可。

## 5. 客户端模块边界

建议保持低耦合：

```text
CallWidget
  |
  v
VideoCallController
  |
  +--> WebRtcBridge
  |
  +--> NetworkManager
```

### CallWidget

负责 UI：

- 显示远端视频区域。
- 显示本地小窗。
- 麦克风、摄像头、挂断、更多按钮。
- 通话状态文案，例如“正在呼叫”“等待对方接听”“通话中”。
- 不直接组装网络 JSON。

### VideoCallController

负责通话流程：

- 发起通话。
- 接收通话请求。
- 接听、拒绝、挂断。
- 创建 callId。
- 控制 WebRTC 页面开始或停止。
- 把 C++ 收到的信令交给 WebRTC 页面。
- 把 WebRTC 页面产生的 offer/answer/ice 交给 NetworkManager。

### WebRtcBridge

负责 C++ 和 JavaScript 交互：

- 暴露给 JS 的 Qt 对象。
- 接收 JS 生成的信令。
- 通知 JS 收到远端信令。
- 接收 JS 的错误信息，例如摄像头不可用。

### NetworkManager

负责 TCP：

- 发送 `call_request`、`call_accept`、`call_reject`、`call_hangup`。
- 发送 `webrtc_offer`、`webrtc_answer`、`webrtc_ice`。
- 接收服务端推送后发出 Qt signal。
- 不处理 UI。

## 6. WebRTC 页面逻辑

HTML/JS 页面建议只负责 WebRTC，不关心业务账号逻辑。

基础流程：

```text
主叫方 Alice
  |
  v
getUserMedia 获取本地摄像头
  |
  v
createPeerConnection
  |
  v
addTrack(localStream)
  |
  v
createOffer
  |
  v
setLocalDescription
  |
  v
把 offer 通过 QWebChannel 发给 C++
  |
  v
服务端转发给 Bob
```

```text
被叫方 Bob
  |
  v
收到 offer
  |
  v
getUserMedia 获取本地摄像头
  |
  v
createPeerConnection
  |
  v
setRemoteDescription(offer)
  |
  v
createAnswer
  |
  v
setLocalDescription
  |
  v
把 answer 发回 Alice
```

双方都需要处理 ICE：

```text
onicecandidate
  |
  v
发送 webrtc_ice
  |
  v
对端 addIceCandidate
```

建议先使用公开 STUN：

```javascript
const rtcConfig = {
  iceServers: [
    { urls: "stun:stun.l.google.com:19302" }
  ]
};
```

如果公网环境无法连通，再加入自建 coturn：

```javascript
const rtcConfig = {
  iceServers: [
    { urls: "stun:8.137.152.134:3478" },
    {
      urls: "turn:8.137.152.134:3478",
      username: "xiaofu",
      credential: "your_password"
    }
  ]
};
```

## 7. FFmpeg 的合理位置

当前主线仍然使用 WebRTC 传输视频。FFmpeg 作为学习和增强模块，不进入核心通话链路。

推荐 FFmpeg 用途：

- 枚举摄像头和麦克风设备。
- 验证摄像头是否能输出 640x480 / 30fps。
- 本地录制通话画面。
- 保存截图。
- 调试花屏、分辨率、像素格式问题。

Windows 设备枚举示例：

```powershell
ffmpeg -list_devices true -f dshow -i dummy
```

摄像头采集测试：

```powershell
ffmpeg -f dshow -video_size 640x480 -framerate 30 -i video="摄像头名称" -t 5 test.mp4
```

Linux 摄像头测试：

```bash
v4l2-ctl --list-devices
ffmpeg -f v4l2 -video_size 640x480 -framerate 30 -i /dev/video0 -t 5 test.mp4
```

注意：不要把 FFmpeg 推到 server 上做实时转码。当前云服务器 2 vCPU / 2 GiB 更适合做 TCP 信令服务器、聊天服务器和 TURN 中继，不适合做视频转码。

## 8. 推荐实现步骤

### 第一步：确认信令闭环

目标：不打开摄像头，只验证通话信令能在两个客户端之间转发。

- A 点击视频通话。
- B 弹出接听/拒绝弹窗。
- B 点击接听。
- A 收到 `call_accept`。
- 任意一方点击挂断。
- 对端收到 `call_hangup` 并关闭通话窗口。

### 第二步：确认 WebRTC 本地预览

目标：只打开本地摄像头，不连接对端。

- Windows 客户端能显示本地摄像头。
- Linux 虚拟机如果没有摄像头，应该显示清晰错误，而不是崩溃。
- 摄像头不存在时 UI 显示“摄像头不可用”。

### 第三步：确认 offer/answer

目标：两个客户端能完成 WebRTC SDP 交换。

- A 产生 offer。
- B 收到 offer 并产生 answer。
- A 收到 answer。
- 控制台打印 `setRemoteDescription success`。

### 第四步：确认 ICE

目标：双方交换 candidate。

- 打印每条 ICE candidate 的类型。
- 能看到 `host`、`srflx` 或 `relay`。
- 如果只有 `host`，跨网络可能失败，需要 TURN。

### 第五步：确认远端视频

目标：Windows + Linux 或两台 Windows 能看到对方视频。

- 设置分辨率 640x480。
- 设置最高 30fps。
- 保持本地小窗和远端主画面比例一致。
- 如果虚拟机摄像头花屏，先用 Windows 双开或另一台电脑测试。

## 9. 常见问题和处理

### Unknown module(s) in QT: webenginewidgets

Linux 缺 Qt WebEngine：

```bash
sudo apt install -y qtwebengine5-dev qtwebengine5-dev-tools libqt5webchannel5-dev
```

### Could not find QtWebEngineProcessd.exe

Windows 运行目录缺 Qt WebEngine 运行时。开发阶段可以从 Qt Creator 启动，打包阶段使用 `windeployqt`：

```powershell
D:\app\Qt5.12.12\msvc2017_64\bin\windeployqt.exe --debug --webengine .\debug\xiaofu-vcall-client.exe
```

### Requested device not found

摄像头不可用。不要让程序崩溃，应该提示：

```text
摄像头不可用，请检查设备或权限
```

### 视频花屏

优先检查：

- 摄像头是否被其他软件占用。
- 是否使用虚拟机摄像头转发。
- 分辨率是否固定为 640x480。
- WebRTC video 元素是否保持 `object-fit: cover` 或 `contain`。
- 是否使用了旧的 Qt WebEngine 内核。

### 公网通话失败

如果聊天和信令正常，但视频无法出现，通常是 NAT 问题。处理顺序：

1. 先确认 offer/answer/ice 都到达。
2. 打印 ICE connection state。
3. 配置 coturn。
4. 在阿里云安全组开放 UDP/TCP 3478。
5. 必要时开放 TURN relay 端口范围。

## 10. 云服务器部署建议

服务端：

```bash
cd /root/workspace/server/build-linux-epoll

./xiaofu-server \
  --host 0.0.0.0 \
  --port 9000 \
  --workers 2 \
  --data-dir /root/xiaofu-vcall-data
```

安全组至少开放：

```text
TCP 9000    xiaofu-vcall 业务服务器
UDP 3478    coturn STUN/TURN
TCP 3478    coturn TURN fallback
```

后续如果使用 TURN relay 端口范围，需要额外开放：

```text
UDP 49152-49200
```

## 11. 日志要求

服务端建议打印：

```text
[INFO] user joined: alice fd=11
[INFO] call signal: call_request from=alice to=bob
[INFO] call signal forwarded: webrtc_offer from=alice to=bob
[WARN] call target offline: bob
```

客户端建议打印：

```text
[Call] request sent to bob
[Call] incoming call from alice
[Call] offer created
[Call] answer received
[Call] ice candidate sent
[Call] ice connection state: connected
[Call] camera error: Requested device not found
```

## 12. 面试表达重点

可以这样解释项目：

```text
我的项目把聊天和视频通话拆成两条链路。

聊天是 TCP 长连接，服务端用 epoll 管理多个客户端连接，用线程池处理数据库任务，SQLite 保存历史消息。

视频通话不让服务端处理媒体流，服务端只转发 WebRTC 信令。真正的摄像头采集、编码、传输、解码和渲染都由客户端 WebRTC 完成。这样服务器压力小，也更符合真实音视频系统的分层思路。

FFmpeg 作为辅助模块，用于设备枚举、本地录制和视频调试，不放在服务端做实时转码。
```

## 13. Claude Code 接手建议

请优先阅读这些文件：

```text
client/xiaofu-vcall-client.pro
client/src/CallWidget.cpp
client/src/CallWidget.h
client/src/video/WebRtcBridge.h
client/src/video/WebRtcBridge.cpp
client/src/video/VideoCallController.h
client/src/video/VideoCallController.cpp
client/resources/video/video_call.html
client/src/network/NetworkManager.h
client/src/network/NetworkManager.cpp
server/src/ServerApp.cpp
server/src/net/EpollLoop.cpp
server/src/net/Connection.cpp
```

接手时不要大改架构，先做小步验证：

1. 保证 call 信令在两个客户端之间稳定转发。
2. 保证 WebRTC 页面本地摄像头预览稳定。
3. 保证 offer/answer/ice 三类消息能通过 `NetworkManager` 转发。
4. 保证无摄像头、权限拒绝、对方离线时都有清晰 UI 提示。
5. 最后再处理 TURN、FFmpeg 录制和 UI 细节。
