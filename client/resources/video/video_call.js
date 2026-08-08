(function () {
    'use strict';

    var BUILD = '2026-08-08-r10.2.4-ui-recorder-fullscreen-fix';

    var required = ['core', 'log', 'state', 'bridge', 'media', 'ice', 'stats', 'probe', 'peer', 'signal', 'ui', 'screen', 'recorder'];
    var missing = [];
    for (var i = 0; i < required.length; i++) {
        if (typeof window.Xiaofu === 'undefined' || !window.Xiaofu[required[i]]) {
            missing.push(required[i]);
        }
    }
    if (missing.length) {
        console.error('[Call] MODULE_MISSING ' + missing.join(','));
        return;
    }

    var X = window.Xiaofu;

    function warn(message) {
        X.log.warn('Call', message);
    }

    function startPreview() {
        X.media.startPreview().catch(function (error) {
            warn('PREVIEW_ERROR ' + error);
        });
    }

    function startOutgoingCall() {
        X.ui.resetLocalPipPosition();
        X.signal.startOutgoingCall().catch(function (error) {
            warn('OUTGOING_ERROR ' + error);
        });
    }

    function acceptIncomingCall() {
        X.state.callActive = true;
        X.ui.resetLocalPipPosition();
        X.log.call('ACCEPT_INCOMING_R10_1_FOUNDATION_FIX');
        X.media.startPreview().catch(function (error) {
            warn('PREVIEW_ERROR ' + error);
        });
    }

    function applyRemoteSignal(signal) {
        X.signal.applyRemoteSignal(signal).catch(function (error) {
            warn('REMOTE_SIGNAL_ERROR ' + error);
        });
    }

    function stopCall() {
        X.state.callToken++;
        X.state.callActive = false;
        X.state.outgoingOfferStarted = false;
        X.state.previewPromise = null;
        X.stats.stopStats();
        X.probe.stopProbe();
        X.recorder.stop();
        X.peer.closePc();
        X.state.pendingCandidates = [];
        X.media.stopLocalStream();
        X.screen.stop();
        X.ui.setScreenShareState(false);
        X.ui.setRecordingState(false);
        var remoteVideo = X.dom.remoteVideo;
        if (remoteVideo) {
            remoteVideo.onloadedmetadata = null;
            remoteVideo.onplaying = null;
            remoteVideo.onresize = null;
            remoteVideo.onwaiting = null;
            remoteVideo.onstalled = null;
            remoteVideo.onerror = null;
            remoteVideo.srcObject = null;
        }
        X.ui.closeMoreMenu();
        X.bridge.reportState('closed');
    }

    function setCameraEnabled(enabled) {
        X.media.setCameraEnabled(enabled);
    }

    function setCameraProfile(name) {
        X.media.setCameraProfile(name).catch(function (error) {
            warn('CAMERA_PROFILE_ERROR ' + error);
        });
    }

    function setCameraProbeEnabled(enabled) {
        X.state.cameraProbeEnabled = !!enabled;
        X.log.call('CAMERA_PROBE_ENABLED=' + X.state.cameraProbeEnabled);
        if (X.state.cameraProbeEnabled && X.media.getVideoTrack(X.state.localStream)) {
            X.probe.startProbe();
        }
    }
    function setIceServers(servers) {
        X.ice.setIceServers(servers);
    }

    function setIcePolicy() {
        X.ice.setIcePolicy();
    }

    function setDiagFilter() {
        X.ice.setDiagFilter();
    }

    function setDiagMaxDumps(value) {
        X.stats.setDiagMaxDumps(value);
    }

    function screenToggle() {
        if (!X.screen.isSupported()) {
            X.log.warn('Call', 'SCREEN_SHARE_UNSUPPORTED_CLICK');
            if (X.ui.showToast) X.ui.showToast('当前版本暂不支持屏幕共享');
            return;
        }
        if (X.state.screen.active) {
            X.screen.stop();
            X.ui.setScreenShareState(false);
            return;
        }
        X.screen.start().catch(function (error) {
            warn('SCREEN_START_FAIL ' + error);
        });
    }

    function recorderToggle() {
        if (X.state.recorder.recording) {
            X.recorder.stop().catch(function (error) {
                warn('RECORDER_STOP_FAIL ' + error);
            });
            return;
        }
        X.recorder.start().catch(function (error) {
            warn('RECORDER_START_FAIL ' + error);
        });
    }

    function isFullscreen() {
        var doc = document;
        return !!(doc.fullscreenElement || doc.webkitFullscreenElement);
    }

    function fullscreenToggle() {
        var doc = document;
        var toggleOn = !isFullscreen();
        X.log.ui('FULLSCREEN_REQUEST toggleOn=' + toggleOn);
        if (!toggleOn) {
            if (doc.exitFullscreen) doc.exitFullscreen();
            else if (doc.webkitExitFullscreen) doc.webkitExitFullscreen();
            return;
        }
        var target = document.getElementById('call-stage') || document.body;
        if (target.requestFullscreen) target.requestFullscreen();
        else if (target.webkitRequestFullscreen) target.webkitRequestFullscreen();
        else X.log.warn('Call', 'FULLSCREEN_API_UNAVAILABLE');
    }

    function onFullscreenChange() {
        var active = isFullscreen();
        if (X.ui.setFullscreenState) X.ui.setFullscreenState(active);
    }

    function bindFeatureButtons() {
        var micButton = document.getElementById('mic-button');
        if (micButton) {
            micButton.addEventListener('click', function () {
                var active = micButton.getAttribute('data-active') !== 'true';
                micButton.setAttribute('data-active', active ? 'true' : 'false');
                X.log.call('MIC_TOGGLE enabled=' + active);
            });
        }
        var camButton = document.getElementById('cam-button');
        if (camButton) {
            camButton.addEventListener('click', function () {
                var unavailable = X.state.localCameraUnavailable === true;
                var track = X.media.getVideoTrack(X.state.localStream);
                if (unavailable || !track || track.readyState !== 'live') {
                    X.log.call('CAM_REACQUIRE_REQUEST');
                    X.media.startPreview().then(function (stream) {
                        if (stream) {
                            X.log.call('CAM_REACQUIRE_OK cameraLabel=' + X.state.cameraDeviceLabel);
                        } else {
                            X.log.warn('Call', 'CAM_REACQUIRE_FAIL');
                        }
                    });
                    return;
                }
                var active = camButton.getAttribute('data-active') !== 'true';
                camButton.setAttribute('data-active', active ? 'true' : 'false');
                X.media.setCameraEnabled(active);
                X.log.call('CAM_TOGGLE enabled=' + active);
            });
        }
        var hangupButton = document.getElementById('hangup-button');
        if (hangupButton) {
            hangupButton.addEventListener('click', function () {
                X.log.call('HANGUP_REQUEST');
                if (X.state.bridge && typeof X.state.bridge.requestHangup === 'function') {
                    X.state.bridge.requestHangup();
                } else {
                    stopCall();
                }
            });
        }
        var shareButton = document.getElementById('share-button');
        if (shareButton) {
            shareButton.addEventListener('click', screenToggle);
        }
        var recordButton = document.getElementById('record-button');
        if (recordButton) {
            recordButton.addEventListener('click', recorderToggle);
        }
        var fullscreenButton = document.getElementById('fullscreen-button');
        if (fullscreenButton) {
            fullscreenButton.addEventListener('click', function () {
                fullscreenToggle();
            });
        }
    }

    function diagnose() {
        X.log.call('MANUAL_DIAG ' + X.peer.pcState(X.state.peerConnection));
        var track = X.media.getVideoTrack(X.state.localStream);
        X.media.logVideoTrack('MANUAL_LOCAL_VIDEO', track);
        var remoteVideo = X.dom.remoteVideo;
        X.log.call('MANUAL_REMOTE_VIDEO readyState=' + (remoteVideo ? remoteVideo.readyState : 'n/a') + ' videoWidth=' + (remoteVideo ? remoteVideo.videoWidth : 0) + ' videoHeight=' + (remoteVideo ? remoteVideo.videoHeight : 0));
        X.log.call('MANUAL_PROBE frames=' + X.state.probeFrames + ' errors=' + X.state.probeErrors + ' hash=' + X.state.probeLastHash + ' source=' + X.state.probeLastWidth + 'x' + X.state.probeLastHeight + ' probeActive=' + X.state.probeActive);
        X.stats.dumpStats('manual');
    }

    function getDiagState() {
        var track = X.media.getVideoTrack(X.state.localStream);
        var settings = X.media.videoTrackSettings(track);
        var remoteVideo = X.dom.remoteVideo;
        var screenState = X.screen.getState();
        var recorderState = X.recorder.getState();
        return {
            build: BUILD,
            video: true,
            audio: false,
            icePolicy: 'relay',
            turnOnly: true,
            canvasProbe: 'local',
            cameraProfile: X.media.getCurrentCameraProfile(),
            cameraGeneration: X.state.cameraGeneration,
            hasTurn: X.ice.hasTurnServer(),
            turnServerCount: X.ice.getTurnIceServers().length,
            pcState: X.peer.pcState(X.state.peerConnection),
            localVideoTrack: track ? {
                readyState: track.readyState,
                enabled: track.enabled,
                muted: track.muted,
                width: settings.width || 0,
                height: settings.height || 0,
                frameRate: settings.frameRate || 0
            } : null,
            remoteVideo: remoteVideo ? {
                readyState: remoteVideo.readyState,
                videoWidth: remoteVideo.videoWidth,
                videoHeight: remoteVideo.videoHeight
            } : null,
            canvas: {
                frames: X.state.probeFrames,
                errors: X.state.probeErrors,
                hash: X.state.probeLastHash,
                width: X.state.probeLastWidth,
                height: X.state.probeLastHeight,
                active: X.state.probeActive
            },
            screen: screenState,
            recorder: recorderState,
            pip: {
                moved: X.state.ui.pipMoved,
                x: X.state.ui.pipX,
                y: X.state.ui.pipY
            },
            pendingCandidates: X.state.pendingCandidates.length
        };
    }

    function getFeatureState() {
        var screenState = X.screen.getState();
        var recorderState = X.recorder.getState();
        return {
            build: BUILD,
            screenShareSupported: screenState.supported,
            screenShareActive: screenState.active,
            mediaRecorderSupported: recorderState.supported,
            mediaRecorderMimeType: recorderState.mimeType,
            pipDraggable: !!(X.dom.localVideo && X.dom.callStage)
        };
    }

    window.xiaofuWebRtc = {
        startPreview: startPreview,
        startOutgoingCall: startOutgoingCall,
        acceptIncomingCall: acceptIncomingCall,
        applyRemoteSignal: applyRemoteSignal,
        stopCall: stopCall,
        setCameraEnabled: setCameraEnabled,
        setCameraProfile: setCameraProfile,
        setIceServers: setIceServers,
        setIcePolicy: setIcePolicy,
        setDiagFilter: setDiagFilter,
        setCameraProbeEnabled: setCameraProbeEnabled,
        setDiagMaxDumps: setDiagMaxDumps,
        screenToggle: screenToggle,
        recorderToggle: recorderToggle,
        diagnose: diagnose,
        getDiagState: getDiagState,
        getFeatureState: getFeatureState
    };

    document.addEventListener('fullscreenchange', onFullscreenChange);
    document.addEventListener('webkitfullscreenchange', onFullscreenChange);
    X.media.init();
    X.ui.init();
    X.ui.buildCameraProfileBar();
    X.screen.init();
    X.recorder.init();
    if (!X.screen.isSupported() && X.ui.setScreenShareUnsupported) {
        X.ui.setScreenShareUnsupported(true);
    }
    bindFeatureButtons();

    X.log.call('R10_READY build=' + BUILD);
    X.log.call('DIAG_CONFIG video=true audio=false width=640 height=480 maxFps=30 icePolicy=relay turnOnly=true canvasProbe=local cameraProfile=vga build=' + BUILD);
}());
