(function (X) {
    'use strict';

    /* 实时字幕：AudioWorklet 采集远端音频 → 16kHz PCM → WebSocket → FunASR → 黑底白字字幕 */

    // 服务地址：由 C++ 桥通过 X.subtitle.setUrl() 注入（来源：asr.url 运行配置 / XIAOFU_ASR_URL 环境变量 / 编译默认）。
    // 这里不维护任何生产或本机兜底地址；未注入时不会发起 WebSocket 连接。
    var ASR_URL = '';

    // AudioWorklet 处理器源码（内嵌字符串，用 Blob URL 加载，避免 qrc:// 的 fetch 限制）。
    // 采用方案 A：AudioContext 以 16000 创建，引擎自动重采样，这里只做 16bit 转换 + 攒 60ms 块。
    var WORKLET_SOURCE = [
        'class SubtitleResampler extends AudioWorkletProcessor {',
        '  constructor() {',
        '    super();',
        '    this.out = new Int16Array(960);',
        '    this.n = 0;',
        '  }',
        '  process(inputs) {',
        '    var ch = inputs[0] && inputs[0][0];',
        '    if (!ch) return true;',
        '    for (var i = 0; i < ch.length; i++) {',
        '      this.out[this.n++] = (Math.max(-1, Math.min(1, ch[i])) * 0x7fff) | 0;',
        '      if (this.n === 960) {',
        '        var buf = this.out.buffer.slice(0);',
        '        this.port.postMessage(buf, [buf]);',
        '        this.out = new Int16Array(960);',
        '        this.n = 0;',
        '      }',
        '    }',
        '    return true;',
        '  }',
        '}',
        'registerProcessor("subtitle-resampler", SubtitleResampler);'
    ].join('\n');

    var MAX_SUBTITLE_CHARS = 50;

    var state = {
        enabled: false,
        ready: false,
        ws: null,
        wsReady: false,
        ctx: null,
        node: null,
        source: null,
        track: null,
        pendingTrack: null,
        bar: null,
        currentText: '',
        hideTimer: 0,
        pcmTotalBytes: 0,
        pcmLastLog: 0
    };

    function log(message) {
        X.log.ui('SUBTITLE ' + message);
    }

    function warn(message) {
        X.log.warn('Subtitle', message);
    }

    function subtitleBar() {
        if (!state.bar) {
            state.bar = document.getElementById('subtitle-bar');
            if (!state.bar) {
                state.bar = document.createElement('div');
                state.bar.id = 'subtitle-bar';
                state.bar.style.whiteSpace = 'nowrap';
                state.bar.style.overflow = 'hidden';
                state.bar.style.textOverflow = 'ellipsis';
                state.bar.style.maxWidth = '82%';
                var stage = document.getElementById('call-stage') || document.body;
                stage.appendChild(state.bar);
            }
        }
        return state.bar;
    }

    function showBar() {
        var bar = subtitleBar();
        if (bar) bar.style.display = 'block';
    }

    function hideBar() {
        if (state.bar) state.bar.style.display = 'none';
    }

    function updateText(text) {
        if (!state.enabled) return;
        if (!text) return;
        var rawLen = text.length;
        var displayText = rawLen > MAX_SUBTITLE_CHARS ? text.slice(rawLen - MAX_SUBTITLE_CHARS) : text;
        state.currentText = displayText;
        var bar = subtitleBar();
        bar.textContent = displayText;
        log('SUBTITLE_RENDER ASR_RAW_LENGTH=' + rawLen + ' SUBTITLE_DISPLAY_LENGTH=' + displayText.length);
        showBar();
        if (state.hideTimer) clearTimeout(state.hideTimer);
        state.hideTimer = setTimeout(hideBar, 4000);
    }

    // C++ 桥在页面加载后调用，覆盖字幕服务地址；未注入时保持为空，连接阶段会提示失败。
    function setUrl(url) {
        ASR_URL = url || '';
        log('URL_SET ' + (ASR_URL || '(none)'));
    }

    function sendHandshake() {
        if (!state.ws || state.ws.readyState !== WebSocket.OPEN) return;
        state.ws.send(JSON.stringify({
            mode: 'online',
            chunk_size: [0, 10, 5],
            encoder_chunk_look_back: 4,
            decoder_chunk_look_back: 1,
            chunk_interval: 10,
            wav_name: 'remote',
            is_speaking: true
        }));
        log('HANDSHAKE_SENT mode=online chunk_size=0,10,5');
    }

    function connectWs() {
        return new Promise(function (resolve, reject) {
            if (!ASR_URL) {
                updateText('字幕服务未配置');
                warn('NO_ASR_URL url not injected');
                reject(new Error('subtitle url not configured'));
                return;
            }
            log('WS_CONNECT_BEGIN url=' + ASR_URL);
            var ws;
            try {
                ws = new WebSocket(ASR_URL);
            } catch (error) {
                reject(error);
                return;
            }
            state.ws = ws;
            ws.binaryType = 'arraybuffer';
            ws.onopen = function () {
                state.wsReady = true;
                log('WS_OPEN');
                sendHandshake();
                resolve();
            };
            ws.onmessage = function (event) {
                if (typeof event.data !== 'string') {
                    log('WS_MESSAGE textLength=-1');
                    return;
                }
                log('WS_MESSAGE textLength=' + event.data.length);
                var msg;
                try {
                    msg = JSON.parse(event.data);
                } catch (error) {
                    return;
                }
                if (msg && msg.text) {
                    if (msg.is_final === true || msg.is_final === 1) {
                        log('ASR_FINAL ASR_RAW_LENGTH=' + msg.text.length);
                    } else {
                        log('ASR_PARTIAL ASR_RAW_LENGTH=' + msg.text.length);
                    }
                    updateText(msg.text);
                }
            };
            ws.onerror = function () {
                log('WS_ERROR');
            };
            ws.onclose = function (event) {
                state.wsReady = false;
                var code = (event && event.code !== undefined) ? event.code : '';
                var reason = (event && event.reason) ? event.reason : '';
                log('WS_CLOSE code=' + code + ' reason=' + reason);
            };
        });
    }

    function attachSource(track) {
        if (!state.ctx || !state.node || !track) return;
        if (state.source) {
            try {
                state.source.disconnect();
            } catch (error) { /* ignore */ }
        }
        var stream;
        try {
            stream = new MediaStream([track]);
        } catch (error) {
            warn('MEDIA_STREAM_CREATE_FAIL ' + error);
            return;
        }
        state.source = state.ctx.createMediaStreamSource(stream);
        // 只连 worklet，不连 destination（扬声器），避免重复播放/回声
        state.source.connect(state.node);
        state.track = track;
        log('REMOTE_TRACK_ATTACHED');
    }

    function setupAudio(track) {
        if (state.ctx) {
            attachSource(track);
            return Promise.resolve();
        }
        var AudioCtx = window.AudioContext || window.webkitAudioContext;
        if (!AudioCtx) {
            warn('AUDIO_CONTEXT_UNSUPPORTED');
            return Promise.reject(new Error('AudioContext unsupported'));
        }
        // 方案 A：指定 16k 采样率，Web Audio 引擎自动把远端音频重采样到 16k
        var ctx = new AudioCtx({ sampleRate: 16000 });
        state.ctx = ctx;
        if (ctx.sampleRate !== 16000) {
            warn('CONTEXT_SAMPLE_RATE ' + ctx.sampleRate + ' EXPECTED 16000');
        }
        var blob = new Blob([WORKLET_SOURCE], { type: 'application/javascript' });
        var url = URL.createObjectURL(blob);
        return ctx.audioWorklet.addModule(url).then(function () {
            URL.revokeObjectURL(url);
            state.node = new AudioWorkletNode(ctx, 'subtitle-resampler');
            state.node.port.onmessage = function (event) {
                if (state.ws && state.wsReady && state.ws.readyState === WebSocket.OPEN) {
                    state.ws.send(event.data);
                    state.pcmTotalBytes += event.data.byteLength;
                    var now = Date.now();
                    if (now - state.pcmLastLog >= 2000) {
                        log('PCM_SEND totalBytes=' + state.pcmTotalBytes);
                        state.pcmLastLog = now;
                    }
                }
            };
            attachSource(track);
            state.ready = true;
            log('AUDIO_READY sampleRate=' + ctx.sampleRate);
        }).catch(function (error) {
            URL.revokeObjectURL(url);
            warn('WORKLET_LOAD_FAIL ' + error);
            throw error;
        });
    }

    function start() {
        if (state.enabled) return;
        state.enabled = true;
        state.pcmTotalBytes = 0;
        state.pcmLastLog = 0;
        showBar();
        updateText('实时字幕已开启…');
        connectWs().catch(function (error) {
            warn('WS_CONNECT_FAIL ' + error);
            updateText(ASR_URL ? '字幕服务连接失败' : '字幕服务未配置');
        });
        if (state.pendingTrack) {
            setupAudio(state.pendingTrack).catch(function (error) {
                warn('AUDIO_SETUP_FAIL ' + error);
            });
        }
        log('START');
    }

    function stop() {
        state.enabled = false;
        if (state.hideTimer) {
            clearTimeout(state.hideTimer);
            state.hideTimer = 0;
        }
        if (state.ws && state.ws.readyState === WebSocket.OPEN) {
            try {
                state.ws.send(JSON.stringify({ is_speaking: false, is_end: true }));
            } catch (error) { /* ignore */ }
        }
        setTimeout(function () {
            if (state.ws) {
                try {
                    state.ws.close();
                } catch (error) { /* ignore */ }
                state.ws = null;
            }
            if (state.ctx) {
                try {
                    state.ctx.close();
                } catch (error) { /* ignore */ }
                state.ctx = null;
            }
            state.node = null;
            state.source = null;
            state.track = null;
            state.ready = false;
            state.wsReady = false;
        }, 300);
        hideBar();
        state.currentText = '';
        log('STOP');
    }

    // 由 peer.js ontrack 调用：接入远端音频 track
    function attachRemoteTrack(track) {
        if (!track || track.kind !== 'audio') return;
        log('REMOTE_AUDIO_TRACK id=' + (track.id || '?') + ' kind=' + track.kind + ' readyState=' + track.readyState + ' enabled=' + track.enabled + ' muted=' + track.muted);
        state.pendingTrack = track;
        if (state.enabled) {
            setupAudio(track).catch(function (error) {
                warn('AUDIO_SETUP_FAIL ' + error);
            });
        }
    }

    function toggle() {
        if (state.enabled) {
            stop();
        } else {
            start();
        }
    }

    function isEnabled() {
        return state.enabled;
    }

    X.subtitle = {
        start: start,
        stop: stop,
        toggle: toggle,
        isEnabled: isEnabled,
        attachRemoteTrack: attachRemoteTrack,
        setUrl: setUrl
    };

    X.log.ui('subtitle loaded');
}(window.Xiaofu = window.Xiaofu || {}));
