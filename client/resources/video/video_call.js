(function () {
    'use strict';

    const localVideo = document.getElementById('local-video');
    const remoteVideo = document.getElementById('remote-video');
    const emptyState = document.getElementById('empty-state');
    const state = {
        bridge: null,
        localStream: null,
        peerConnection: null,
        pendingCandidates: [],
        cameraEnabled: true
    };

    function notifyError(error) {
        const message = error && error.message ? error.message : String(error);
        if (state.bridge) {
            state.bridge.reportCallError(message);
        }
        emptyState.textContent = '摄像头不可用：' + message;
    }

    function notifyState(callState) {
        if (state.bridge) {
            state.bridge.reportCallState(callState);
        }
    }

    function createPeerConnection() {
        if (state.peerConnection) {
            return state.peerConnection;
        }

        const peerConnection = new RTCPeerConnection();
        peerConnection.onicecandidate = function (event) {
            if (event.candidate && state.bridge) {
                state.bridge.reportOutgoingSignal({
                    type: 'ice_candidate',
                    candidate: event.candidate.candidate,
                    sdpMid: event.candidate.sdpMid,
                    sdpMLineIndex: event.candidate.sdpMLineIndex
                });
            }
        };
        peerConnection.ontrack = function (event) {
            remoteVideo.srcObject = event.streams[0];
            emptyState.style.display = 'none';
        };
        peerConnection.onconnectionstatechange = function () {
            notifyState(peerConnection.connectionState);
        };
        state.peerConnection = peerConnection;
        return peerConnection;
    }

    async function startPreview() {
        if (state.localStream) {
            return state.localStream;
        }

        // 第一阶段只采集视频，避免未实现的音频链路占用麦克风权限。
        const stream = await navigator.mediaDevices.getUserMedia({
            video: {
                width: { ideal: 640 },
                height: { ideal: 480 },
                frameRate: { ideal: 30, max: 30 }
            },
            audio: false
        });
        state.localStream = stream;
        localVideo.srcObject = stream;
        emptyState.style.display = 'none';

        const track = stream.getVideoTracks()[0];
        if (track && state.bridge) {
            state.bridge.reportPreviewReady(track.getSettings());
        }
        return stream;
    }

    async function startOutgoingCall() {
        try {
            const stream = await startPreview();
            const peerConnection = createPeerConnection();
            stream.getTracks().forEach(function (track) {
                peerConnection.addTrack(track, stream);
            });
            const offer = await peerConnection.createOffer();
            await peerConnection.setLocalDescription(offer);
            state.bridge.reportOutgoingSignal({ type: 'webrtc_offer', sdp: offer.sdp });
        } catch (error) {
            notifyError(error);
        }
    }

    async function applyRemoteSignal(signal) {
        try {
            const peerConnection = createPeerConnection();
            if (signal.type === 'webrtc_offer') {
                await peerConnection.setRemoteDescription({ type: 'offer', sdp: signal.sdp });
                const stream = await startPreview();
                stream.getTracks().forEach(function (track) {
                    peerConnection.addTrack(track, stream);
                });
                const answer = await peerConnection.createAnswer();
                await peerConnection.setLocalDescription(answer);
                state.bridge.reportOutgoingSignal({ type: 'webrtc_answer', sdp: answer.sdp });
            } else if (signal.type === 'webrtc_answer') {
                await peerConnection.setRemoteDescription({ type: 'answer', sdp: signal.sdp });
            } else if (signal.type === 'ice_candidate') {
                const candidate = {
                    candidate: signal.candidate,
                    sdpMid: signal.sdpMid,
                    sdpMLineIndex: signal.sdpMLineIndex
                };
                if (peerConnection.remoteDescription) {
                    await peerConnection.addIceCandidate(candidate);
                } else {
                    state.pendingCandidates.push(candidate);
                }
            }

            while (peerConnection.remoteDescription && state.pendingCandidates.length > 0) {
                await peerConnection.addIceCandidate(state.pendingCandidates.shift());
            }
        } catch (error) {
            notifyError(error);
        }
    }

    function setCameraEnabled(enabled) {
        state.cameraEnabled = enabled;
        if (state.localStream) {
            state.localStream.getVideoTracks().forEach(function (track) {
                track.enabled = enabled;
            });
        }
    }

    function stopCall() {
        if (state.localStream) {
            state.localStream.getTracks().forEach(function (track) { track.stop(); });
            state.localStream = null;
        }
        if (state.peerConnection) {
            state.peerConnection.close();
            state.peerConnection = null;
        }
        state.pendingCandidates = [];
        localVideo.srcObject = null;
        remoteVideo.srcObject = null;
        emptyState.style.display = 'flex';
        emptyState.textContent = '等待视频通话…';
        notifyState('closed');
    }

    function makeLocalVideoDraggable() {
        let offsetX = 0;
        let offsetY = 0;
        let dragging = false;
        localVideo.addEventListener('pointerdown', function (event) {
            dragging = true;
            const rect = localVideo.getBoundingClientRect();
            offsetX = event.clientX - rect.left;
            offsetY = event.clientY - rect.top;
            localVideo.setPointerCapture(event.pointerId);
        });
        localVideo.addEventListener('pointermove', function (event) {
            if (!dragging) return;
            const stageRect = document.getElementById('call-stage').getBoundingClientRect();
            const left = Math.max(12, Math.min(event.clientX - stageRect.left - offsetX,
                stageRect.width - localVideo.offsetWidth - 12));
            const top = Math.max(12, Math.min(event.clientY - stageRect.top - offsetY,
                stageRect.height - localVideo.offsetHeight - 12));
            localVideo.style.left = left + 'px';
            localVideo.style.top = top + 'px';
            localVideo.style.right = 'auto';
        });
        localVideo.addEventListener('pointerup', function () { dragging = false; });
    }

    window.xiaofuWebRtc = {
        startPreview: function () { startPreview().catch(notifyError); },
        startOutgoingCall: startOutgoingCall,
        applyRemoteSignal: applyRemoteSignal,
        setCameraEnabled: setCameraEnabled,
        stopCall: stopCall,
        acceptIncomingCall: function () { startPreview().catch(notifyError); }
    };

    makeLocalVideoDraggable();
    new QWebChannel(qt.webChannelTransport, function (channel) {
        state.bridge = channel.objects.webRtcBridge;
        notifyState('ready');
    });
}());
