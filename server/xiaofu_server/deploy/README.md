# Linux 服务端构建与部署

服务端采用 C++17、epoll ET、2 个数据库工作线程、eventfd 完成队列和 SQLite WAL。
WebRTC 视频只经过服务端转发信令，视频编码、解码和媒体流均在客户端完成。

## Ubuntu 本地编译

```bash
sudo apt update
sudo apt install -y build-essential cmake libsqlite3-dev libssl-dev

cd ~/xiaofu_vcall
cmake -S xiaofu_server -B xiaofu_server/build-linux-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build xiaofu_server/build-linux-debug --target xiaofu-server -j"$(nproc)"

./xiaofu_server/build-linux-debug/xiaofu-server \
  --host 0.0.0.0 --port 9000 --workers 2 \
  --data-dir "$HOME/xiaofu-vcall-data"
```

确认监听状态：

```bash
ss -lntp | grep 9000
```

## 云服务器 systemd 部署

在仓库根目录执行：

```bash
sudo bash xiaofu_server/deploy/install_ubuntu.sh
```

常用管理命令：

```bash
sudo systemctl status xiaofu-server
sudo journalctl -u xiaofu-server -f
sudo systemctl restart xiaofu-server
```

systemd 默认以前台方式管理进程。只有手工演示传统守护进程时才使用：

```bash
./xiaofu-server --daemon --data-dir /var/lib/xiaofu-vcall
```

## 客户端测试地址覆盖

客户端默认连接 `8.137.152.134:9000`。需要连接局域网或虚拟机服务端时：

Linux：

```bash
XIAOFU_SERVER_HOST=10.7.154.8 XIAOFU_SERVER_PORT=9000 ./debug/xiaofu-vcall-client
```

Windows PowerShell：

```powershell
$env:XIAOFU_SERVER_HOST = "127.0.0.1"
$env:XIAOFU_SERVER_PORT = "9000"
./debug/xiaofu-vcall-client.exe
```
