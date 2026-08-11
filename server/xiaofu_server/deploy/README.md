# Linux 服务端构建与部署

服务端采用 C++17、epoll ET、2 个数据库工作线程、eventfd 完成队列，
数据持久化使用本机 MySQL（127.0.0.1:3306，业务库 xiaofu）。
连接参数由 systemd 注入：/etc/xiaofu-server.env（XIAOFU_MYSQL_*，权限 600）。
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

在仓库根目录执行：

```bash
sudo bash xiaofu_server/deploy/install_ubuntu.sh
```

部署前需准备 MySQL 业务账号与凭据文件（密码只写入 600 权限的 env 文件，不进 Git/日志）：

```bash
sudo install -m 0600 -o root -g root /dev/null /etc/xiaofu-server.env
sudo sh -c 'echo "XIAOFU_MYSQL_HOST=127.0.0.1" >> /etc/xiaofu-server.env'
sudo sh -c 'echo "XIAOFU_MYSQL_PORT=3306" >> /etc/xiaofu-server.env'
sudo sh -c 'echo "XIAOFU_MYSQL_USER=xiaofu" >> /etc/xiaofu-server.env'
sudo sh -c 'echo "XIAOFU_MYSQL_PASSWORD=<业务账号密码>" >> /etc/xiaofu-server.env'
sudo sh -c 'echo "XIAOFU_MYSQL_DATABASE=xiaofu" >> /etc/xiaofu-server.env'
sudo chmod 600 /etc/xiaofu-server.env
```

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
