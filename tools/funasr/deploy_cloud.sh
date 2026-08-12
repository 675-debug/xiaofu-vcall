#!/usr/bin/env bash
# ============================================================
# FunASR 实时字幕 - 云服务器 Ubuntu 22.04 一键部署脚本（纯 CPU；部署目标：腾讯云）
# 部署目录：/root/workspace/model/funser
# 用法：cd /root/workspace/model/funser && sudo bash deploy_cloud.sh
# 前置：funasr_wss_server.py 已上传到 /root/workspace/model/funser/
# ============================================================
set -euo pipefail

APP_DIR="/root/workspace/model/funser"
PORT="10095"
PUBLIC_HOST="${PUBLIC_HOST:-127.0.0.1}"   # 部署后替换为服务器公网 IP（腾讯云），不要提交真实 IP
PY="${APP_DIR}/.venv/bin/python"

if [ "$(id -u)" -ne 0 ]; then
  echo "请用 root 或 sudo 执行"; exit 1
fi

mkdir -p "$APP_DIR"
cd "$APP_DIR"
if [ ! -f "${APP_DIR}/funasr_wss_server.py" ]; then
  echo "缺少 funasr_wss_server.py，请先上传到 ${APP_DIR}/"; exit 1
fi

echo "==> [1/6] 系统依赖"
export DEBIAN_FRONTEND=noninteractive
apt-get update -y
apt-get install -y python3 python3-venv python3-pip

echo "==> [2/6] 内存保底：无 swap 时创建 4GB swap（2G 内存跑 5 个模型容易 OOM）"
if [ "$(swapon --show 2>/dev/null | wc -l)" -eq 0 ]; then
  fallocate -l 4G /swapfile || dd if=/dev/zero of=/swapfile bs=1M count=4096
  chmod 600 /swapfile
  mkswap /swapfile
  swapon /swapfile
  grep -q "^/swapfile" /etc/fstab || echo "/swapfile none swap sw 0 0" >> /etc/fstab
  echo "swap 4G 已启用并写入 /etc/fstab"
else
  echo "已有 swap，跳过"
fi

echo "==> [3/6] Python 虚拟环境 + 依赖（CPU 版 torch）"
if [ ! -d "${APP_DIR}/.venv" ]; then
  python3 -m venv "${APP_DIR}/.venv"
fi
"$PY" -m pip install --upgrade pip
"$PY" -m pip install torch --index-url https://download.pytorch.org/whl/cpu
"$PY" -m pip install funasr==1.4.1 modelscope==1.39.1 websockets==17.0.1 numpy==2.4.6 scipy==1.18.0

echo "==> [4/6] 下载/校验模型（MODELSCOPE_CACHE=${APP_DIR}，模型统一放在这里）"
download_model() {
  local name="$1" rev="${2:-}"
  echo "==> 下载: $name"
  MODELSCOPE_CACHE="${APP_DIR}" "$PY" -c "
import os
from funasr import AutoModel
name = os.environ['M']
rev = os.environ.get('R') or None
kw = dict(model=name, ngpu=0, device='cpu', disable_pbar=True, disable_log=True)
if rev:
    kw['model_revision'] = rev
AutoModel(**kw)
print('OK', name)
" M="$name" R="$rev"
}
download_model "paraformer-zh" "v2.0.4"
download_model "iic/speech_paraformer-large_asr_nat-zh-cn-16k-common-vocab8404-online" "v2.0.4"
download_model "iic/speech_fsmn_vad_zh-cn-16k-common-pytorch" "v2.0.4"
download_model "iic/punc_ct-transformer_zh-cn-common-vad_realtime-vocab272727" "v2.0.4"
download_model "iic/speech_campplus_sv_zh-cn_16k-common"

echo "==> [5/6] 写 systemd 服务（监听 0.0.0.0:${PORT}，低并发，开机自启）"
cat > /etc/systemd/system/funasr.service <<EOF
[Unit]
Description=FunASR WebSocket Subtitle Server
After=network.target

[Service]
WorkingDirectory=${APP_DIR}
ExecStart=${PY} ${APP_DIR}/funasr_wss_server.py --host 0.0.0.0 --port ${PORT} --ngpu 0 --device cpu --ncpu 2 --worker_threads 2 --concurrent_vad 2 --concurrent_asr_online 1 --concurrent_asr_offline 1 --concurrent_punc 1 --concurrent_sv 1 --certfile "" --keyfile ""
Restart=always
RestartSec=5
Environment=PYTHONUNBUFFERED=1
Environment=MODELSCOPE_CACHE=${APP_DIR}
StandardOutput=append:/var/log/funasr.log
StandardError=append:/var/log/funasr.log

[Install]
WantedBy=multi-user.target
EOF

echo "==> [6/6] 启动服务"
systemctl daemon-reload
systemctl enable funasr
systemctl restart funasr
sleep 3
systemctl status funasr --no-pager || true

echo ""
echo "完成。检查命令："
echo "  systemctl status funasr"
echo "  ss -lntp | grep ${PORT}"
echo "  tail -f /var/log/funasr.log"
echo "客户端连接地址：ws://${PUBLIC_HOST}:${PORT}"