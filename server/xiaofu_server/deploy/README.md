# Linux 服务端构建与部署

服务端采用 C++17、epoll ET、2 个数据库工作线程、eventfd 完成队列，
数据持久化使用本机 MySQL（127.0.0.1:3306，业务库 xiaofu）。
服务参数和 MySQL 连接参数由 systemd 注入：`/etc/xiaofu-server.env`（权限 600）。
WebRTC 视频只经过服务端转发信令，视频编码、解码和媒体流均在客户端完成。

## Ubuntu 本地编译

```bash
sudo apt update
sudo apt install -y build-essential cmake libmysqlclient-dev libssl-dev

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

## 云服务器 systemd 部署（MySQL 版）

首次部署时，先从示例创建权限为 600 的配置并填写 MySQL 密码：

```bash
sudo install -o root -g root -m 0600 \
  xiaofu_server/deploy/xiaofu-server.env.example /etc/xiaofu-server.env
sudo editor /etc/xiaofu-server.env
```

然后执行安装：

```bash
sudo bash xiaofu_server/deploy/install_ubuntu.sh
```

如果配置文件尚不存在，安装脚本也会从示例自动创建；所有后续升级均保留该文件，
不会覆盖真实配置。自动创建的示例密码为空，服务正常连接 MySQL 前必须填写。

可配置项包括 `XIAOFU_SERVER_HOST`、`XIAOFU_SERVER_PORT`、
`XIAOFU_SERVER_WORKERS`、`XIAOFU_DATA_DIR` 以及 `XIAOFU_MYSQL_*`。
命令行的 `--host`、`--port`、`--workers`、`--data-dir` 会覆盖对应环境变量。
密码只写入该 600 权限文件，不进入 Git 或日志。

MySQL 业务账号仅允许 localhost 访问，禁止在阿里云安全组放通 3306。
旧 SQLite 数据迁移与校验：`tools/migrate_sqlite_to_mysql.py`。

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
