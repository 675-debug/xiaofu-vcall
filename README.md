# xiaofu-vcall

基于 Qt/C++、Linux epoll、WebRTC 和 FunASR 的桌面即时通信项目。

## 项目组成

| 模块 | 说明 |
|---|---|
| `client/` | Qt 6 Widgets 客户端，负责登录、联系人、聊天和视频通话 |
| `server/xiaofu_server/` | C++17 业务服务，使用 epoll、线程池、eventfd 和 MySQL |
| `server/FunASR_Server/` | Python WebSocket 实时中文语音识别服务 |

业务消息使用 `4 字节大端长度头 + JSON payload`。音视频通过 WebRTC P2P/TURN 传输，业务服务只转发 Offer、Answer 和 ICE；字幕音频由客户端直接发送给 FunASR 服务。

## 下载源码

```bash
git clone https://github.com/675-debug/xiaofu-vcall.git
cd xiaofu-vcall
```

仓库只提供不含密码的配置模板。请复制模板后填写自己的服务器地址、数据库密码、TURN 凭据和模型路径。真实配置文件已被 `.gitignore` 排除。

## Windows Client

使用 Qt 6.11.1、MSVC2022 64-bit 构建。构建会把 `config.ini.example` 复制到 exe 目录；首次运行会在同目录创建 `config.ini`。也可以手动创建：

```powershell
Copy-Item client/config.ini.example "D:\path\to\app\config.ini"
notepad "D:\path\to\app\config.ini"
```

环境变量优先级高于 `config.ini`。可配置业务服务器、STUN/TURN、ICE 策略、FunASR WebSocket 地址和 WebEngine 性能选项。不要把填写了密码的 `config.ini` 提交到 Git。

## Ubuntu 业务 Server

要求 Ubuntu 22.04 和 MySQL 8。安装脚本会编译服务、安装二进制与 systemd unit；如果 `/etc/xiaofu-server.env` 已存在，不会覆盖它。

```bash
sudo apt update
sudo apt install -y build-essential cmake libmysqlclient-dev libssl-dev

sudo install -o root -g root -m 0600 \
  server/xiaofu_server/deploy/xiaofu-server.env.example \
  /etc/xiaofu-server.env
sudo editor /etc/xiaofu-server.env

sudo bash server/xiaofu_server/deploy/install_ubuntu.sh
sudo systemctl status xiaofu-server --no-pager -l
sudo journalctl -u xiaofu-server -f
```

`/etc/xiaofu-server.env` 可配置监听地址、端口、工作线程、数据目录及全部 MySQL 连接参数。`XIAOFU_MYSQL_PASSWORD` 必须由部署者填写，示例文件不会包含真实密码。详细说明见 [`server/xiaofu_server/deploy/README.md`](server/xiaofu_server/deploy/README.md)。

## Ubuntu FunASR Server

模型、虚拟环境和运行日志不进入 Git。先准备 FunASR/Paraformer streaming 模型，再创建 Python 环境：

```bash
cd server/FunASR_Server
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt

sudo install -o root -g root -m 0600 \
  deploy/funasr-server.env.example /etc/xiaofu-asr.env
sudo editor /etc/xiaofu-asr.env

sudo install -o root -g root -m 0644 \
  deploy/xiaofu-asr.service /etc/systemd/system/xiaofu-asr.service
sudo systemctl daemon-reload
sudo systemctl enable --now xiaofu-asr.service
sudo systemctl status xiaofu-asr.service --no-pager -l
sudo journalctl -u xiaofu-asr.service -f
```

必须把 `/etc/xiaofu-asr.env` 中的 `FUNASR_MODEL_DIR` 改为本机真实模型目录。模板 service 默认项目位于 `/home/ubuntu/workspace/xiaofu-vcall/server/FunASR_Server`；若下载到其他目录，请同步修改 service 的 `WorkingDirectory` 和 `ExecStart`。协议和自测方法见 [`server/FunASR_Server/README.md`](server/FunASR_Server/README.md)。

## 配置文件规则

| 模块 | 提交到 Git 的模板 | 本机/服务器真实配置 |
|---|---|---|
| Client | `client/config.ini.example` | exe 同目录 `config.ini` |
| 业务 Server | `server/xiaofu_server/deploy/xiaofu-server.env.example` | `/etc/xiaofu-server.env` |
| FunASR Server | `server/FunASR_Server/deploy/funasr-server.env.example` | `/etc/xiaofu-asr.env` |

不要提交密码、Token、TURN credential、模型文件、`venv/`、日志、数据库文件、构建目录和发布包。

## License

[MIT License](LICENSE)
