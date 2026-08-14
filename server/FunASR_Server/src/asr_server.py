#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""FunASR_Server: WebSocket Streaming ASR (paraformer-zh-streaming, CPU).

协议 (与 client/resources/video/webrtc/subtitle.js 兼容):
  - 连接后客户端先发 JSON handshake: {mode, chunk_size, encoder_chunk_look_back,
    decoder_chunk_look_back, chunk_interval, wav_name, is_speaking}
  - 之后发送 binary PCM16 little-endian, 16kHz mono, 每块 960 samples = 60ms = 1920 bytes
  - 结束发送 {"is_speaking": false, "is_end": true}
  - 服务端返回 JSON, 客户端只显示含 text 字段的消息:
      {"type": "handshake_ack", "status": "ok", ...}
      {"type": "partial", "text": "<累计字幕>"}
      {"type": "final",   "text": "<累计字幕>"}
      {"type": "error", "code": "...", "message": "..."}

模型聚合粒度 (FunASR 1.4.1, paraformer-streaming):
  chunk_stride_samples = chunk_size[1] * 960; chunk_size=[0,10,5] => 9600 samples = 600ms
  => 服务端把多个 60ms PCM 包累积到 9600 samples 后再调用一次 model.generate。
"""

import argparse
import asyncio
import json
import logging
import os
import signal
import sys
import time
from pathlib import Path

import numpy as np
import psutil
import websockets

from funasr import AutoModel

BASE_DIR = Path(__file__).resolve().parent.parent

# 协议常量 (来自 subtitle.js)
DEFAULT_HOST = "0.0.0.0"          # 外部客户端 (Windows) 可通过腾讯云服务器地址访问
DEFAULT_PORT = 10095
CHUNK_SIZE = [0, 10, 5]          # 模型 chunk 配置 (600ms @16k)
CHUNK_SAMPLES = CHUNK_SIZE[1] * 960   # 9600 samples
FRAME_SAMPLES = 960              # 60ms 网络帧
FRAME_BYTES = FRAME_SAMPLES * 2  # 1920 bytes
FINAL_MIN_SAMPLES = 960          # 模型 final 尾块低于该值会走 tail_chunk 旧特征路径
# 71M 小模型在流式 600ms chunk 下 look_back=(0,0) 质量最佳 (实测 4/1 反而叠字更多)。
# 客户端 handshake 中的 encoder/decoder_chunk_look_back 字段仍接受, 但服务端固定使用本值。
LOOK_BACK = {"encoder_chunk_look_back": 0, "decoder_chunk_look_back": 0}

log = logging.getLogger("asr_server")


class ConnectionState:
    """每个连接独立维护 streaming cache / PCM buffer / speaking 状态 / 文本累计。"""

    def __init__(self):
        self.cache = {}                # 模型 streaming cache (连接独有)
        self.pcm = bytearray()         # 未推理 PCM16 缓冲
        self.text_parts = []           # 已识别文本片段 (增量)
        self.wav_name = "remote"
        self.handshaken = False
        self.finalized = False
        self.chunk_count = 0           # 已推理的 600ms chunk 数
        self.frame_count = 0           # 已收到的 60ms 网络帧数
        self.samples_in = 0            # 收到的 PCM sample 总数
        self.started_at = time.monotonic()
        self.infer_times = []          # 最近推理耗时 (s)
        self.last_stats_at = time.monotonic()

    def cumulative_text(self):
        return "".join(self.text_parts)

    def reset_utterance(self):
        """一句话结束后的清理 (cache 由模型 is_final 自动重建, 这里显式重置兜底)。"""
        self.cache = {}
        self.pcm = bytearray()
        self.text_parts = []
        self.chunk_count = 0
        self.finalized = False


class AsrServer:
    def __init__(self, model_dir, host=DEFAULT_HOST, port=DEFAULT_PORT,
                 max_connections=4, max_pcm_samples=960000):
        self.host = host
        self.port = port
        self.max_connections = max_connections
        self.max_pcm_samples = max_pcm_samples
        self.active_conns = set()      # 当前活跃连接集合 (<= max_connections)
        self.infer_lock = asyncio.Lock()  # AutoModel 共享实例串行调用
        self.stopping = False
        self.proc = psutil.Process(os.getpid())
        self._cpu_baseline = None
        self.low_ram_streak = 0        # available RAM 持续低于阈值计数

        log.info("加载模型 (仅一次, 常驻): %s", model_dir)
        t0 = time.time()
        self.model = AutoModel(model=model_dir, device="cpu", disable_update=True,
                               disable_pbar=True, log_level="WARNING")
        self.model_dir = str(model_dir)
        load_s = time.time() - t0
        rss = self._rss_mb()
        log.info("模型加载完成, 耗时 %.1fs, RSS=%.0fMB", load_s, rss)
        # 安全红线: 只允许 71M 小模型; RSS 异常(>1.8GB)立即退出
        if rss > 1800:
            log.error("MODEL_RSS=%.0fMB 超过 1.8GB 红线, 立即退出", rss)
            sys.exit(3)

    # ---------- 工具 ----------
    def _rss_mb(self):
        return self.proc.memory_info().rss / 1024.0 / 1024.0

    def _cpu_percent(self):
        pct = self.proc.cpu_percent(interval=None)
        if self._cpu_baseline is None:
            self._cpu_baseline = pct
            return 0.0
        return pct

    async def send_json(self, ws, payload):
        try:
            await ws.send(json.dumps(payload, ensure_ascii=False))
        except websockets.exceptions.ConnectionClosed:
            pass

    async def send_error(self, ws, code, message):
        await self.send_json(ws, {"type": "error", "code": code, "message": message})

    def _infer_sync(self, st, pcm_bytes, is_final):
        """同步执行一次模型推理 (单连接下阻塞可接受)。"""
        samples = np.frombuffer(pcm_bytes, dtype="<i2").astype(np.float32) / 32768.0
        t0 = time.monotonic()
        kwargs = dict(
            input=samples, cache=st.cache, is_final=is_final,
            chunk_size=CHUNK_SIZE, disable_pbar=True, **LOOK_BACK,
        )
        res = self.model.generate(**kwargs)
        dt = time.monotonic() - t0
        text = ""
        if res and isinstance(res, list) and "text" in res[0]:
            text = res[0]["text"] or ""
        st.text_parts.append(text)
        st.chunk_count += 1
        st.infer_times.append(dt)
        if len(st.infer_times) > 50:
            st.infer_times.pop(0)
        return text, dt

    async def infer(self, ws, st, pcm_bytes, is_final):
        # 每个连接有独立 cache，但共享 AutoModel 是否线程安全没有保证。
        # 2C4G 部署下串行推理也能避免多个模型调用同时抢占内存和 CPU。
        async with self.infer_lock:
            text, dt = await asyncio.get_running_loop().run_in_executor(
                None, self._infer_sync, st, pcm_bytes, is_final)
        return text, dt

    # ---------- 消息处理 ----------
    async def on_text(self, ws, st, message):
        try:
            msg = json.loads(message)
        except Exception:
            await self.send_error(ws, "bad_json", "invalid JSON")
            return
        if not isinstance(msg, dict):
            await self.send_error(ws, "bad_message", "expected JSON object")
            return

        is_end = msg.get("is_end") is True or msg.get("is_speaking") is False
        if is_end:
            if not st.handshaken:
                await self.send_error(ws, "no_handshake", "end before handshake")
                return
            await self.finalize(ws, st)
            return

        if not st.handshaken:
            if msg.get("mode") != "online":
                await self.send_error(ws, "no_handshake",
                                      "handshake requires mode=online")
                return
            st.wav_name = str(msg.get("wav_name", "remote"))
            st.handshaken = True
            log.info("HANDSHAKE mode=%s chunk_size=%s wav_name=%s",
                     msg.get("mode"), msg.get("chunk_size"), st.wav_name)
            await self.send_json(ws, {"type": "handshake_ack", "status": "ok",
                                      "mode": msg.get("mode")})
            return

        # 已 handshake 后的额外 JSON (如热词等): 忽略, 不崩溃
        await self.send_json(ws, {"type": "ack"})

    async def on_binary(self, ws, st, message):
        if not st.handshaken:
            await self.send_error(ws, "no_handshake", "binary data before handshake")
            await ws.close(code=1008, reason="binary before handshake")
            return
        if len(message) == 0:
            return
        if len(message) % 2 != 0:
            await self.send_error(ws, "bad_pcm", "PCM16 data must have even byte length")
            return

        st.pcm.extend(message)
        st.samples_in += len(message) // 2
        st.frame_count += 1

        # 累计到 600ms (9600 samples) 再推理, 禁止每 1920B 调一次 generate
        while len(st.pcm) >= CHUNK_SAMPLES * 2:
            chunk = bytes(st.pcm[:CHUNK_SAMPLES * 2])
            del st.pcm[:CHUNK_SAMPLES * 2]
            text, dt = await self.infer(ws, st, chunk, is_final=False)
            log.debug("chunk=%d infer=%.3fs text=%r backlog=%dB",
                      st.chunk_count, dt, text, len(st.pcm))
            await self.send_json(ws, {"type": "partial", "text": st.cumulative_text()})

        # 资源保护: 消费速度不足时不允许无限缓存
        if len(st.pcm) // 2 > self.max_pcm_samples:
            log.warning("PCM backlog 超限: %d samples > %d, 关闭连接",
                        len(st.pcm) // 2, self.max_pcm_samples)
            await self.send_error(ws, "buffer_overflow",
                                  f"pcm backlog exceeded {self.max_pcm_samples} samples")
            await ws.close(code=1013, reason="pcm backlog overflow")
            return

        await self.maybe_stats(ws, st, force=False)

    async def finalize(self, ws, st):
        if st.finalized:
            return
        st.finalized = True
        tail = bytes(st.pcm)
        st.pcm.clear()
        log.info("FINALIZE wav=%s tail_samples=%d parts=%d",
                 st.wav_name, len(tail) // 2, len(st.text_parts))

        if len(tail) > 0:
            n = len(tail) // 2
            if n < FINAL_MIN_SAMPLES:
                # 安全补齐: 避免 <960 samples 的尾块触发模型 tail_chunk 旧特征路径
                tail = tail + b"\x00\x00" * (FINAL_MIN_SAMPLES - n)
                log.info("final tail padded %d -> %d samples", n, FINAL_MIN_SAMPLES)
            text, dt = await self.infer(ws, st, tail, is_final=True)
            log.info("final infer=%.3fs text=%r", dt, text)
        elif st.chunk_count > 0:
            # 没有剩余 PCM 时仍需触发 is_final，让流式模型冲刷 cache。
            tail = b"\x00\x00" * FINAL_MIN_SAMPLES
            text, dt = await self.infer(ws, st, tail, is_final=True)
            log.info("final flush infer=%.3fs text=%r", dt, text)

        final_text = st.cumulative_text()
        log.info("FINAL wav=%s text=%r", st.wav_name, final_text)
        await self.send_json(ws, {"type": "final", "text": final_text})
        st.reset_utterance()

    async def maybe_stats(self, ws, st, force=False):
        now = time.monotonic()
        if not force and now - st.last_stats_at < 5.0:
            return
        st.last_stats_at = now
        avg = (sum(st.infer_times) / len(st.infer_times)) if st.infer_times else 0.0
        avail_mb = psutil.virtual_memory().available / 1024.0 / 1024.0
        log.info("STATS rss=%.0fMB avail=%.0fMB cpu=%.1f%% backlog=%dB frames=%d chunks=%d avg_infer=%.3fs",
                 self._rss_mb(), avail_mb, self._cpu_percent(), len(st.pcm),
                 st.frame_count, st.chunk_count, avg)
        # 安全红线: available RAM 持续低于 1GB 且下降 -> 停止服务
        if avail_mb < 1000:
            self.low_ram_streak += 1
            log.error("AVAILABLE_RAM=%.0fMB 低于 1GB (连续 %d 次), 停止服务防止 OOM",
                      avail_mb, self.low_ram_streak)
            if self.low_ram_streak >= 3:
                self.stopping = True
        else:
            self.low_ram_streak = 0

    # ---------- 连接生命周期 ----------
    async def handle(self, ws):
        if len(self.active_conns) >= self.max_connections:
            log.warning("REJECT 连接 (server_busy), 当前活跃 %d/%d",
                        len(self.active_conns), self.max_connections)
            await self.send_error(ws, "server_busy", "already one active connection")
            await ws.close(code=1013, reason="server busy")
            return

        self.active_conns.add(ws)
        st = ConnectionState()
        log.info("CONNECT open (活跃 %d/%d)", len(self.active_conns), self.max_connections)
        try:
            async for message in ws:
                if self.stopping:
                    break
                if isinstance(message, str):
                    await self.on_text(ws, st, message)
                else:
                    await self.on_binary(ws, st, message)
                await self.maybe_stats(ws, st)
        except websockets.exceptions.ConnectionClosed as exc:
            log.info("CONNECT closed by client (code=%s) frames=%d chunks=%d",
                     exc.code, st.frame_count, st.chunk_count)
        except Exception:
            log.exception("连接处理异常")
        finally:
            self.active_conns.discard(ws)
            st.reset_utterance()
            log.info("CONNECT cleanup done (活跃 %d/%d), RSS=%.0fMB",
                     len(self.active_conns), self.max_connections, self._rss_mb())

    # ---------- 启动 ----------
    async def run(self):
        log.info("绑定 %s:%d, MAX_CONNECTIONS=%d", self.host, self.port, self.max_connections)
        async with websockets.serve(self.handle, self.host, self.port,
                                    max_size=8 * 1024 * 1024):
            log.info("READY ws://%s:%d (模型 RSS=%.0fMB)", self.host, self.port, self._rss_mb())
            while not self.stopping:
                await asyncio.sleep(0.5)
        log.info("STOPPED")


def main():
    parser = argparse.ArgumentParser(description="FunASR streaming ASR WebSocket server (loopback)")
    parser.add_argument("--host", default=os.environ.get("FUNASR_ASR_HOST", DEFAULT_HOST))
    parser.add_argument("--port", type=int, default=int(os.environ.get("FUNASR_ASR_PORT", DEFAULT_PORT)))
    parser.add_argument("--max-connections", type=int,
                        default=int(os.environ.get("FUNASR_MAX_CONNECTIONS", 4)))
    parser.add_argument("--max-pcm-samples", type=int,
                        default=int(os.environ.get("FUNASR_MAX_PCM_SAMPLES", 960000)))
    parser.add_argument("--model-dir", default=os.environ.get("FUNASR_MODEL_DIR", ""))
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        stream=sys.stdout,
    )
    logging.getLogger("funasr").setLevel(logging.WARNING)

    # 固定使用 71M 小模型 (iic/speech_paraformer_asr_nat-zh-cn-16k-common-vocab8404-online),
    # 严禁回退到 large 模型
    SMALL_MODEL_NAME = "iic--speech_paraformer_asr_nat-zh-cn-16k-common-vocab8404-online"
    model_dir = args.model_dir
    if not model_dir:
        candidates = sorted((BASE_DIR / "model" / "models").glob("*/snapshots/*"))
        small = [str(c) for c in candidates if SMALL_MODEL_NAME in str(c)]
        if small:
            model_dir = small[0]
        elif candidates:
            log.error("未找到 71M 小模型 (%s); 找到: %s, 禁止加载 large 模型, 退出",
                      SMALL_MODEL_NAME, [str(c) for c in candidates])
            sys.exit(2)
    if not model_dir or not Path(model_dir).exists():
        log.error("未找到模型目录: %s", model_dir)
        sys.exit(2)
    if "large" in model_dir:
        log.error("模型目录包含 large (%s), 红线禁止, 退出", model_dir)
        sys.exit(2)

    server = AsrServer(model_dir, host=args.host, port=args.port,
                       max_connections=args.max_connections,
                       max_pcm_samples=args.max_pcm_samples)

    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, lambda: setattr(server, "stopping", True))
        except NotImplementedError:
            pass
    try:
        loop.run_until_complete(server.run())
    except KeyboardInterrupt:
        pass
    finally:
        loop.close()


if __name__ == "__main__":
    main()
