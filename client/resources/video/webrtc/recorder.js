(function (X) {
    'use strict';

    var supported = false;
    var vp8Supported = false;
    var recorder = null;
    var chunks = [];
    var stream = null;
    var canvas = null;
    var canvasCtx = null;
    var drawTimer = null;
    var onStopped = null;
    var saveCounter = 0;

    function detect() {
        supported = typeof MediaRecorder !== 'undefined';
        X.state.recorder.supported = supported;
        X.log.recorder('CAPABILITY MediaRecorder=' + supported);
        if (supported && typeof MediaRecorder.isTypeSupported === 'function') {
            vp8Supported = !!MediaRecorder.isTypeSupported('video/webm;codecs=vp8');
            var plain = !!MediaRecorder.isTypeSupported('video/webm');
            X.log.recorder('CAPABILITY video/webm=' + plain + ' video/webm;codecs=vp8=' + vp8Supported);
        }
        if (supported) {
            X.state.recorder.mimeType = vp8Supported ? 'video/webm;codecs=vp8' : 'video/webm';
        }
    }

    function isSupported() {
        return supported;
    }

    function isRecording() {
        return !!(recorder && recorder.state === 'recording');
    }

    function pad2(n) {
        return n < 10 ? '0' + n : '' + n;
    }

    function drawCover(ctx, video, x, y, w, h) {
        var vw = video.videoWidth || 1;
        var vh = video.videoHeight || 1;
        var scale = Math.max(w / vw, h / vh);
        var dw = vw * scale;
        var dh = vh * scale;
        var dx = x + (w - dw) / 2;
        var dy = y + (h - dh) / 2;
        try {
            ctx.drawImage(video, dx, dy, dw, dh);
        } catch (error) {
            X.log.warn('Recorder', 'DRAW_FAIL ' + error);
        }
    }

    function drawFrame() {
        if (!canvas || !canvasCtx) return;
        var remote = X.dom.remoteVideo;
        var local = X.dom.localVideo;
        var width = canvas.width;
        var height = canvas.height;
        canvasCtx.fillStyle = '#101018';
        canvasCtx.fillRect(0, 0, width, height);
        if (remote && remote.videoWidth) {
            drawCover(canvasCtx, remote, 0, 0, width, height);
        }
        if (local && local.videoWidth) {
            var pw = Math.round(width * 0.32);
            var ph = pw && local.videoWidth ? Math.round(pw * local.videoHeight / local.videoWidth) : Math.round(width * 0.24);
            drawCover(canvasCtx, local, width - pw - 12, 12, pw, ph);
        }
    }

    function ensureCanvas() {
        if (canvas) return canvas;
        canvas = document.createElement('canvas');
        canvas.width = 1280;
        canvas.height = 720;
        canvas.style.display = 'none';
        canvasCtx = canvas.getContext('2d');
        return canvas;
    }

    function startDraw() {
        if (drawTimer) return;
        drawFrame();
        drawTimer = setInterval(drawFrame, 66);
    }

    function stopDraw() {
        if (drawTimer) {
            clearInterval(drawTimer);
            drawTimer = null;
        }
    }

    function cleanupMedia() {
        stopDraw();
        if (stream) {
            stream.getTracks().forEach(function (track) {
                try {
                    track.stop();
                } catch (error) {
                }
            });
            stream = null;
        }
        recorder = null;
        X.state.recorder.recording = false;
        X.state.recorder.startedAt = 0;
    }

    function saveBlob(blob) {
        X.log.recorder('SAVE_REQUEST bytes=' + blob.size);
        if (!blob || blob.size === 0) return;
        try {
            var url = URL.createObjectURL(blob);
            var a = document.createElement('a');
            var now = new Date();
            var stamp = now.getFullYear() + '' + pad2(now.getMonth() + 1) + '' + pad2(now.getDate()) + '-' + pad2(now.getHours()) + '' + pad2(now.getMinutes()) + '' + pad2(now.getSeconds());
            saveCounter++;
            a.href = url;
            a.download = 'vcall-' + stamp + '-' + saveCounter + '.webm';
            a.style.display = 'none';
            // 程序化下载 click 不应冒泡成“页面点击”，避免误关更多菜单。
            a.addEventListener('click', function (event) {
                if (event && event.stopPropagation) event.stopPropagation();
            });
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            setTimeout(function () {
                URL.revokeObjectURL(url);
            }, 10000);
        } catch (error) {
            X.log.warn('Recorder', 'SAVE_FAIL ' + error);
        }
    }

    function finalizeBlob() {
        var mimeType = X.state.recorder.mimeType || 'video/webm';
        var blob = new Blob(chunks, { type: mimeType });
        chunks = [];
        X.log.recorder('BLOB_READY bytes=' + blob.size);
        saveBlob(blob);
        return blob;
    }

    function start() {
        if (!supported) {
            X.log.recorder('START_REJECTED unsupported');
            return Promise.reject(new Error('MediaRecorder unsupported'));
        }
        if (isRecording()) {
            return Promise.resolve();
        }
        var remote = X.dom.remoteVideo;
        if (!remote || !remote.srcObject) {
            X.log.recorder('START_REJECTED no-remote');
            return Promise.reject(new Error('no remote video'));
        }
        X.log.recorder('START');
        ensureCanvas();
        if (remote.videoWidth && remote.videoHeight) {
            canvas.width = remote.videoWidth;
            canvas.height = remote.videoHeight;
        }
        var mimeType = X.state.recorder.mimeType || 'video/webm';
        if (typeof MediaRecorder.isTypeSupported === 'function' && !MediaRecorder.isTypeSupported(mimeType)) {
            mimeType = 'video/webm';
            X.state.recorder.mimeType = mimeType;
        }
        try {
            stream = canvas.captureStream(30);
        } catch (error) {
            X.log.recorder('START_REJECTED captureStream ' + error);
            return Promise.reject(new Error('captureStream unavailable'));
        }
        chunks = [];
        try {
            recorder = new MediaRecorder(stream, {
                mimeType: mimeType,
                videoBitsPerSecond: 2500000
            });
        } catch (error) {
            try {
                recorder = new MediaRecorder(stream);
            } catch (error2) {
                X.log.recorder('START_REJECTED MediaRecorder ' + error2);
                return Promise.reject(new Error('MediaRecorder construct failed'));
            }
        }
        recorder.ondataavailable = function (event) {
            if (event.data && event.data.size > 0) chunks.push(event.data);
        };
        recorder.onerror = function (event) {
            X.log.warn('Recorder', 'ERROR ' + (event && event.error ? event.error : 'unknown'));
            if (X.state.recorder.recording) {
                X.state.recorder.recording = false;
                X.state.recorder.startedAt = 0;
                if (X.ui.setRecordingState) X.ui.setRecordingState(false);
            }
        };
        recorder.onstop = function () {
            stopDraw();
            X.log.recorder('STOP');
            X.state.recorder.recording = false;
            X.state.recorder.startedAt = 0;
            if (X.ui.setRecordingState) X.ui.setRecordingState(false);
            var blob = finalizeBlob();
            cleanupMedia();
            if (onStopped) {
                var callback = onStopped;
                onStopped = null;
                callback(blob);
            }
        };
        startDraw();
        X.state.recorder.recording = true;
        X.state.recorder.startedAt = Date.now();
        if (X.ui.setRecordingState) X.ui.setRecordingState(true);
        X.log.recorder('RECORDING mimeType=' + mimeType + ' canvas=' + canvas.width + 'x' + canvas.height);
        recorder.start(1000);
        return Promise.resolve();
    }

    function stop() {
        if (onStopped) {
            return Promise.resolve(null);
        }
        if (!recorder || recorder.state === 'inactive') {
            X.state.recorder.recording = false;
            X.state.recorder.startedAt = 0;
            if (X.ui.setRecordingState) X.ui.setRecordingState(false);
            cleanupMedia();
            return Promise.resolve(null);
        }
        return new Promise(function (resolve) {
            onStopped = function (blob) {
                resolve(blob);
            };
            try {
                recorder.stop();
            } catch (error) {
                onStopped = null;
                cleanupMedia();
                X.log.warn('Recorder', 'STOP_FAIL ' + error);
                resolve(null);
            }
        });
    }

    function getState() {
        return {
            supported: supported,
            recording: isRecording() || X.state.recorder.recording,
            mimeType: X.state.recorder.mimeType
        };
    }

    function init() {
        detect();
    }

    X.recorder = {
        init: init,
        isSupported: isSupported,
        start: start,
        stop: stop,
        getState: getState
    };

    X.log.recorder('loaded');
}(window.Xiaofu = window.Xiaofu || {}));
