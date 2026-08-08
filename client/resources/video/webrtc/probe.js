(function (X) {
    'use strict';

    function infoLine(text) {
        var line = document.createElement('div');
        line.textContent = text;
        line.style.height = '16px';
        line.style.lineHeight = '16px';
        line.style.paddingLeft = '8px';
        line.style.fontFamily = 'monospace';
        line.style.fontSize = '11px';
        line.style.color = '#00ff00';
        line.style.background = 'rgba(0,0,0,0)';
        line.style.overflow = 'hidden';
        line.style.whiteSpace = 'nowrap';
        return line;
    }

    function profileButton(profile, label) {
        var button = document.createElement('button');
        button.type = 'button';
        button.textContent = label;
        button.style.margin = '2px';
        button.style.padding = '4px 6px';
        button.style.fontFamily = 'monospace';
        button.style.fontSize = '11px';
        button.style.cursor = 'pointer';
        button.style.background = '#222';
        button.style.color = '#00ff00';
        button.style.border = '1px solid #00ff00';
        button.style.borderRadius = '4px';
        button.onclick = function () {
            X.log.probe('CAMERA_PROFILE_BUTTON profile=' + profile.name + ' width=' + profile.width + ' height=' + profile.height);
            X.media.setCameraProfile(profile.name).catch(function (error) {
                X.log.warn('Probe', 'CAMERA_PROFILE_SWITCH_FAIL ' + error);
            });
        };
        return button;
    }

    function ensureProbe() {
        if (!X.state.cameraProbeEnabled) return;
        if (X.state.probeCanvas && X.state.probePanel && X.state.probeCtx) {
            return;
        }
        var panel = document.createElement('div');
        panel.id = 'xiaofu-r9-camera-panel';
        panel.style.position = 'absolute';
        panel.style.left = '16px';
        panel.style.top = '16px';
        panel.style.width = '322px';
        panel.style.zIndex = '99999';
        panel.style.background = 'rgba(0,0,0,0.88)';
        panel.style.border = '2px solid #00ff00';
        panel.style.borderRadius = '8px';
        panel.style.overflow = 'hidden';
        panel.style.pointerEvents = 'none';
        panel.style.boxSizing = 'border-box';
        var label = document.createElement('div');
        label.textContent = 'R10.2 Camera Probe';
        label.style.height = '24px';
        label.style.lineHeight = '24px';
        label.style.paddingLeft = '8px';
        label.style.fontFamily = 'monospace';
        label.style.fontSize = '12px';
        label.style.color = '#00ff00';
        label.style.background = '#111';
        panel.appendChild(label);

        var info = document.createElement('div');
        info.style.background = 'rgba(0,0,0,0)';
        var profileLine = infoLine('Profile: -');
        var requestedLine = infoLine('Requested: -');
        var actualLine = infoLine('Actual: -');
        var videoLine = infoLine('Video: readyState=-');
        var trackLine = infoLine('Track: -');
        var canvasLine = infoLine('Canvas: -');
        var framesLine = infoLine('Frames: 0');
        var errorsLine = infoLine('Errors: 0');
        info.appendChild(profileLine);
        info.appendChild(requestedLine);
        info.appendChild(actualLine);
        info.appendChild(videoLine);
        info.appendChild(trackLine);
        info.appendChild(canvasLine);
        info.appendChild(framesLine);
        info.appendChild(errorsLine);
        panel.appendChild(info);

        var canvas = document.createElement('canvas');
        canvas.width = 640;
        canvas.height = 480;
        canvas.style.display = 'block';
        canvas.style.width = '320px';
        canvas.style.height = '240px';
        canvas.style.background = '#000';
        panel.appendChild(canvas);

        var buttonRow = document.createElement('div');
        buttonRow.style.padding = '4px';
        buttonRow.style.pointerEvents = 'auto';
        var profiles = X.media.cameraProfiles || {};
        var visible = X.media.getUIVisibleProfiles ? X.media.getUIVisibleProfiles() : [];
        for (var i = 0; i < visible.length; i++) {
            var vp = visible[i];
            if (!vp) continue;
            buttonRow.appendChild(profileButton(vp, vp.label));
        }
        if (X.state.cameraProbeEnabled && X.media.getExperimentalProfiles) {
            var experimental = X.media.getExperimentalProfiles();
            for (var j = 0; j < experimental.length; j++) {
                var ep = experimental[j];
                if (!ep) continue;
                buttonRow.appendChild(profileButton(ep, ep.label + ' exp'));
            }
        }
        panel.appendChild(buttonRow);

        var parent = document.body || document.documentElement;
        parent.appendChild(panel);
        X.state.probePanel = panel;
        X.state.probeCanvas = canvas;
        X.state.probeCtx = canvas.getContext('2d');
        X.state.probeInfo = {
            profile: profileLine,
            requested: requestedLine,
            actual: actualLine,
            video: videoLine,
            track: trackLine,
            canvas: canvasLine,
            frames: framesLine,
            errors: errorsLine
        };
        X.log.probe('PROBE_PANEL_CREATED width=640 height=480 css=320x240');
    }

    function updateInfo() {
        if (!X.state.probeInfo) return;
        var localVideo = X.dom.localVideo;
        var track = X.media.getVideoTrack(X.state.localStream);
        var settings = X.media.videoTrackSettings(track);
        var profile = X.state.currentCameraProfile || '?';
        var profiles = X.media.cameraProfiles || {};
        var def = profiles[profile];
        var requested = def ? (def.width + 'x' + def.height + ' max30') : (profile + '?');
        var actual = settings.width ? (settings.width + 'x' + settings.height + ' @' + (settings.frameRate || '?')) : 'none';
        X.state.probeInfo.profile.textContent = 'Profile: ' + profile;
        X.state.probeInfo.requested.textContent = 'Requested: ' + requested;
        X.state.probeInfo.actual.textContent = 'Actual: ' + actual;
        X.state.probeInfo.video.textContent = 'Video: readyState=' + (localVideo ? localVideo.readyState : '?');
        X.state.probeInfo.track.textContent = 'Track: ' + (track ? track.readyState : 'none');
        X.state.probeInfo.canvas.textContent = 'Canvas: ' + (X.state.probeLastWidth ? X.state.probeLastWidth + 'x' + X.state.probeLastHeight : 'none') + (X.state.probeActive ? ' active' : ' paused');
        X.state.probeInfo.frames.textContent = 'Frames: ' + X.state.probeFrames;
        X.state.probeInfo.errors.textContent = 'Errors: ' + X.state.probeErrors;
    }

    function frameHash() {
        if (!X.state.probeCtx || !X.state.probeCanvas) return 0;
        try {
            var w = X.state.probeCanvas.width;
            var h = X.state.probeCanvas.height;
            var sampleWidth = 16;
            var sampleHeight = 16;
            var startX = Math.max(0, Math.floor(w / 2 - sampleWidth / 2));
            var startY = Math.max(0, Math.floor(h / 2 - sampleHeight / 2));
            var image = X.state.probeCtx.getImageData(startX, startY, sampleWidth, sampleHeight);
            var data = image.data;
            var hash = 2166136261;
            for (var i = 0; i < data.length; i += 4) {
                hash ^= data[i];
                hash = Math.imul(hash, 16777619);
                hash ^= data[i + 1];
                hash = Math.imul(hash, 16777619);
                hash ^= data[i + 2];
                hash = Math.imul(hash, 16777619);
            }
            return hash >>> 0;
        } catch (error) {
            X.log.warn('Probe', 'PROBE_HASH_FAIL ' + error);
            return 0;
        }
    }

    function drawLocalFrame() {
        var localVideo = X.dom.localVideo;
        if (!localVideo || !X.state.probeCanvas || !X.state.probeCtx) return;
        if (localVideo.readyState < 2 || !localVideo.videoWidth || !localVideo.videoHeight) {
            X.state.probeActive = false;
            return;
        }
        var width = localVideo.videoWidth;
        var height = localVideo.videoHeight;
        if (width <= 2 || height <= 2) {
            X.state.probeActive = false;
            if (!X.state.probeInvalidWarned) {
                X.state.probeInvalidWarned = true;
                X.log.probe('LOCAL_PROBE_INVALID_SOURCE width=' + width + ' height=' + height);
            }
            return;
        }
        if (X.state.probeInvalidWarned) {
            X.state.probeInvalidWarned = false;
            X.log.probe('LOCAL_PROBE_SOURCE_RECOVERED width=' + width + ' height=' + height);
        }
        X.state.probeActive = true;
        if (X.state.probeCanvas.width !== width || X.state.probeCanvas.height !== height) {
            X.state.probeCanvas.width = width;
            X.state.probeCanvas.height = height;
            X.state.probeCanvas.style.width = '320px';
            X.state.probeCanvas.style.height = '240px';
            X.state.probeLastWidth = width;
            X.state.probeLastHeight = height;
            X.log.probe('LOCAL_CANVAS_RESIZE source=' + width + 'x' + height);
        }
        try {
            X.state.probeCtx.drawImage(localVideo, 0, 0, width, height);
            X.state.probeFrames++;
            if (X.state.probeFrames === 1 || X.state.probeFrames % 10 === 0) {
                var hash = frameHash();
                X.state.probeLastHash = hash;
                X.log.probe('LOCAL_CANVAS_FRAME count=' + X.state.probeFrames + ' source=' + width + 'x' + height + ' videoReadyState=' + localVideo.readyState + ' hash=' + hash);
            }
        } catch (error) {
            X.state.probeErrors++;
            X.log.warn('Probe', 'LOCAL_CANVAS_DRAW_FAIL count=' + X.state.probeErrors + ' error=' + error);
        }
        updateInfo();
    }

    function startProbe() {
        ensureProbe();
        if (X.state.probeTimer) return;
        X.state.probeFrames = 0;
        X.state.probeErrors = 0;
        X.state.probeLastHash = 0;
        X.state.probeActive = false;
        X.state.probeInvalidWarned = false;
        X.state.probeTimer = setInterval(drawLocalFrame, 100);
        X.log.probe('LOCAL_PROBE_START intervalMs=100');
        var localVideo = X.dom.localVideo;
        if (localVideo) {
            localVideo.onloadedmetadata = function () {
                X.log.probe('LOCAL_VIDEO_METADATA width=' + localVideo.videoWidth + ' height=' + localVideo.videoHeight + ' readyState=' + localVideo.readyState);
            };
            localVideo.onplaying = function () {
                X.log.probe('LOCAL_VIDEO_PLAYING width=' + localVideo.videoWidth + ' height=' + localVideo.videoHeight + ' readyState=' + localVideo.readyState);
            };
        }
    }

    function stopProbe() {
        if (X.state.probeTimer) {
            clearInterval(X.state.probeTimer);
            X.state.probeTimer = null;
        }
        X.state.probeActive = false;
        if (X.state.probeCtx && X.state.probeCanvas) {
            try {
                X.state.probeCtx.clearRect(0, 0, X.state.probeCanvas.width, X.state.probeCanvas.height);
            } catch (error) {
            }
        }
        X.log.probe('LOCAL_PROBE_STOP frames=' + X.state.probeFrames + ' errors=' + X.state.probeErrors + ' lastHash=' + X.state.probeLastHash);
    }

    X.probe = {
        ensureProbe: ensureProbe,
        frameHash: frameHash,
        updateInfo: updateInfo,
        startProbe: startProbe,
        stopProbe: stopProbe
    };

    X.log.probe('loaded');
}(window.Xiaofu = window.Xiaofu || {}));
