# FunASR_Server（腾讯云 AI 推理服务）

独立部署的 AI 推理服务目录，与业务通信服务 `../xiaofu_server/` 分离部署。

## 职责边界

- 本服务只做 AI 推理与字幕输出，不处理登录、好友、聊天、CallSession、SQLite、WebRTC 信令、TURN。
- 业务通信服务（阿里云）负责 subtitle 字幕 JSON 的业务转发，不运行 FunASR、不处理 PCM、不进行 AI 推理。

## 输入 / 输出约定

- 输入：16kHz / mono PCM（最终格式后续确定）
- 传输：WebSocket
- 推理：FunASR / Paraformer（Paraformer-zh-streaming；VAD / PUNC / 2pass 后续逐步增加）
- 输出：partial / final 字幕

## 部署目标

- 腾讯云。

## 目录说明

- `src/`：服务端源码
- `config/`：配置文件
- `scripts/`：启动 / 部署脚本
- `model/`：模型目录（不提交 Git，运行时放置）
- `logs/`：运行日志目录（不提交 Git）
- `venv/`：Python 虚拟环境（在腾讯云创建，不提交仓库）
