# xiaofu-vcall

一个基于 Qt Widgets 和 C++ 的视频通话客户端/服务端项目。当前版本已完成桌面端 UI、账户认证、TCP 长连接协议、服务端连接管理和真实文字聊天；通话信令与 FFmpeg 媒体链路将在后续迭代实现。

## 当前能力

- Qt Widgets 客户端：登录、注册、找回密码、联系人、聊天和视频通话页面。
- 1650 × 1000 工作区界面，通话页支持麦克风、摄像头、更多菜单、全屏等本地交互。
- “我的视频”悬浮窗可拖拽、缩放，并自动限制在通话画面边界内。
- 基于 `QTcpSocket` 的客户端网络层，统一处理 TCP 连接、长度头拆包和 JSON 响应分发。
- Windows `select` 事件循环服务端，支持多连接接入、断线清理、注册、登录、找回密码、在线会话和超时检查。
- 在线实时文字聊天：客户端登录成功后自动加入在线会话，服务端按接收方路由消息。
- SQLite 用户与聊天记录持久化：支持离线消息保存、重连拉取双向历史、删除单个会话和清空当前账号全部聊天记录。
- SQLite 联系人资料与在线状态：联系人单向保存；注册时填写昵称并生成稳定的随机字母头像，服务端通过 `presence_push` 实时同步上线/离线状态。
- 联系人右键菜单：可删除与该联系人的聊天记录；在线联系人可直接进入视频聊天页面。
- SQLite 用户数据持久化与 SHA-256 密码哈希。

## 项目结构

```text
xiaofu-vcall/
├── client/                 # Qt Widgets 客户端
│   ├── src/                # 页面组件与 NetworkManager
│   ├── ui/                 # Qt Designer 界面文件
│   ├── resources/          # 图标等静态资源
│   └── xiaofu-vcall-client.pro
├── server/                 # C++ TCP 服务端
│   ├── src/net/            # TcpServer、Connection、事件循环
│   ├── src/handler/        # 注册、登录、找回密码、在线会话、聊天路由
│   ├── src/db/             # SQLite 与密码哈希
│   ├── src/protocol/       # JSON 与结果码
│   └── CMakeLists.txt
└── tools/                  # 可读性审计脚本
```

## 架构与协议

客户端页面不会直接读写 socket，而是统一通过 `NetworkManager` 发送请求。协议格式为：

```text
4 字节大端 payloadLength + JSON payload
```

服务端由 `EventLoopWin` 监听 socket 可读事件；`Connection` 负责缓存、拆帧和回写；`main.cpp` 根据 JSON 的 `type` 分发给业务 Handler；Handler 再通过 `DbManager` 访问 SQLite。

目前已接通的请求类型：`register`、`login`、`forgot`、`join`、`heartbeat`、`add_contact`、`contacts`、`presence_push`、`chat`、`history`、`delete_chat`、`clear_chats`。

## 开发环境

- Windows 10/11
- Qt 5.12+，MinGW 7.3+（客户端）
- CMake 3.16+（服务端）
- SQLite 3

服务端当前默认使用 Windows socket 和 `select`，并监听 `127.0.0.1:9000`。

## 构建与运行

### 1. 构建服务端

```powershell
cd server
cmake -S . -B build
cmake --build build --target xiaofu-server unit_test chat_handler_test -j 2
.\build\unit_test.exe
.\build\chat_handler_test.exe
.\build\xiaofu-server.exe
```

> 如果本机 SQLite 不在 Qt MinGW 目录，请在 CMake 配置时传入 `SQLITE_INCLUDE_DIR` 与 `SQLITE_LIB`。

### 2. 构建客户端

```powershell
cd client
qmake xiaofu-vcall-client.pro
mingw32-make -j2
.\debug\xiaofu-vcall-client.exe
```

客户端启动后会连接本机 `127.0.0.1:9000`，请先启动服务端。

## 测试

```powershell
# 服务端 JSON 与密码哈希单元测试
cd server
.\build\unit_test.exe

# 客户端 UI 静态审计
cd ..\client
powershell -ExecutionPolicy Bypass -File tools\ui_static_audit.ps1

# 客户端与服务端可读性审计
cd ..
powershell -ExecutionPolicy Bypass -File tools\code_readability_audit.ps1

# 启动临时服务端并验证双客户端聊天完整链路
powershell -ExecutionPolicy Bypass -File tools\chat_integration_test.ps1
```

## 路线图

- [x] 真实文字聊天消息路由、SQLite 历史消息与离线拉取。
- [ ] 通话邀请、接听、拒绝、挂断等信令状态机。
- [ ] 客户端 join、心跳与断线状态同步。
- [ ] FFmpeg 摄像头/麦克风采集、本地预览与设备控制。
- [ ] 局域网媒体传输、对端解码渲染与项目演示材料。
