# FunASR_Server 占位 CMake 工程
#
# 注意：FunASR_Server 是 Python AI 推理服务（WebSocket + FunASR/Paraformer），
# 不包含 C/C++ 源码。此文件仅为让 CLion 能以“浏览工程”方式打开本目录，
# 不参与任何构建，也不影响腾讯云上的 Python 部署（部署请使用 src/ 内的 Python 代码）。
cmake_minimum_required(VERSION 3.16)
project(FunASR_Server LANGUAGES NONE)

add_custom_target(funasr_server_browse
    COMMENT "FunASR_Server is a Python service. Browse src/ here, or open with PyCharm/VS Code."
)
