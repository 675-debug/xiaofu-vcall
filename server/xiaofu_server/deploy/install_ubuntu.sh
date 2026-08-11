#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
    echo "请使用 sudo 运行：sudo bash xiaofu_server/deploy/install_ubuntu.sh"
    exit 1
fi

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
build_dir="${project_root}/xiaofu_server/build-linux-release"

apt-get update
apt-get install -y build-essential cmake libmysqlclient-dev libssl-dev

cmake -S "${project_root}/xiaofu_server" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}" --target xiaofu-server -j"$(nproc)"

if ! id xiaofu >/dev/null 2>&1; then
    useradd --system --home /var/lib/xiaofu-vcall --shell /usr/sbin/nologin xiaofu
fi
install -d -o xiaofu -g xiaofu /opt/xiaofu-vcall/bin /var/lib/xiaofu-vcall
install -m 0755 "${build_dir}/xiaofu-server" /opt/xiaofu-vcall/bin/xiaofu-server
install -m 0644 "${project_root}/xiaofu_server/deploy/xiaofu-server.service" \
    /etc/systemd/system/xiaofu-server.service

systemctl daemon-reload
systemctl enable --now xiaofu-server
systemctl --no-pager --full status xiaofu-server
