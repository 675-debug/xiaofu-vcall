// 音频设备诊断脚本：项目启动（通话页加载）时自动在控制台输出音频设备相关信息，
// 无需发起通话即可定位音频问题。也可在控制台手动执行 window.audioDebug 下的函数。
(function () {
    'use strict';

    var TAG = '[AudioDebug]';

    function log(message) {
        console.log(TAG + ' ' + message);
    }

    function safeString(value) {
        try {
            return JSON.stringify(value);
        } catch (error) {
            return String(value);
        }
    }

    function formatDevice(device) {
        return {
            kind: device.kind,
            label: device.label || '(无标签：未授权或系统未提供)',
            deviceId: device.deviceId || '(系统默认)',
            groupId: device.groupId || ''
        };
    }

    // 枚举所有媒体设备，重点输出音频输入/输出。
    async function dumpDevices() {
        log('===== 音频设备枚举开始 =====');
        if (!navigator.mediaDevices || !navigator.mediaDevices.enumerateDevices) {
            log('当前环境不支持 navigator.mediaDevices.enumerateDevices');
            return;
        }
        try {
            var devices = await navigator.mediaDevices.enumerateDevices();
            var audioInputs = devices.filter(function (d) { return d.kind === 'audioinput'; });
            var audioOutputs = devices.filter(function (d) { return d.kind === 'audiooutput'; });
            var videoInputs = devices.filter(function (d) { return d.kind === 'videoinput'; });
            log('设备总数: ' + devices.length +
                '（音频输入 ' + audioInputs.length +
                '，音频输出 ' + audioOutputs.length +
                '，视频输入 ' + videoInputs.length + '）');
            log('--- 音频输入（麦克风）---');
            if (audioInputs.length === 0) {
                log('  未检测到麦克风设备');
            }
            audioInputs.forEach(function (d) { log('  ' + safeString(formatDevice(d))); });
            log('--- 音频输出（扬声器/耳机）---');
            if (audioOutputs.length === 0) {
                log('  未检测到音频输出设备');
            }
            audioOutputs.forEach(function (d) { log('  ' + safeString(formatDevice(d))); });
            log('--- 视频输入（摄像头，参考）---');
            videoInputs.forEach(function (d) { log('  ' + safeString(formatDevice(d))); });
            log('===== 音频设备枚举结束 =====');
        } catch (error) {
            log('enumerateDevices 异常: ' + (error && error.message ? error.message : String(error)));
        }
    }

    // 探测麦克风是否能被 getUserMedia 采集到；采集成功后立即停止，不占用设备。
    async function probeMicrophone() {
        log('===== 麦克风采集探测开始 =====');
        if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
            log('当前环境不支持 navigator.mediaDevices.getUserMedia');
            return;
        }
        try {
            var stream = await navigator.mediaDevices.getUserMedia({
                audio: {
                    echoCancellation: { ideal: true },
                    noiseSuppression: { ideal: true },
                    autoGainControl: { ideal: true }
                },
                video: false
            });
            var track = stream.getAudioTracks()[0];
            var settings = null;
            if (track && typeof track.getSettings === 'function') {
                try { settings = track.getSettings(); } catch (e) { settings = null; }
            }
            log('麦克风采集成功: ' + safeString({
                label: track ? track.label : '(无音频轨道)',
                enabled: track ? track.enabled : null,
                muted: track ? track.muted : null,
                settings: settings
            }));
            stream.getTracks().forEach(function (t) { t.stop(); });
            log('测试轨道已立即停止，未占用麦克风');
        } catch (error) {
            var name = error && error.name ? error.name : '';
            var message = error && error.message ? error.message : String(error);
            log('麦克风采集失败: ' + (name ? name + ' - ' : '') + message);
            if (name === 'NotAllowedError' || name === 'PermissionDeniedError') {
                log('  -> 原因：音频权限未授权。当前客户端只放行摄像头权限（CallWidget 的 featurePermissionRequested 只同意 MediaVideoCapture），麦克风权限需要先实现音频授权。');
            } else if (name === 'NotFoundError' || name === 'DevicesNotFoundError') {
                log('  -> 原因：系统没有可用麦克风设备，或设备被拔出。');
            } else if (name === 'NotReadableError' || name === 'TrackStartError') {
                log('  -> 原因：麦克风被其他程序占用，或驱动异常。');
            } else if (name === 'OverconstrainedError' || name === 'ConstraintNotSatisfiedError') {
                log('  -> 原因：当前约束没有匹配的麦克风设备。');
            }
        }
        log('===== 麦克风采集探测结束 =====');
    }

    // 页面加载即自动执行一次：客户端启动时无需发起通话即可在控制台看到音频信息。
    dumpDevices().then(probeMicrophone);

    // 手动入口，可在控制台重复执行：window.audioDebug.dumpDevices() / window.audioDebug.probeMicrophone()
    window.audioDebug = {
        dumpDevices: dumpDevices,
        probeMicrophone: probeMicrophone
    };
}());