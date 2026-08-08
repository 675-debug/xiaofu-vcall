(function (X) {
    'use strict';

    function pcState(pc) {
        if (!pc) return 'pc=null';
        return 'signaling=' + pc.signalingState + ' ice=' + pc.iceConnectionState + ' gathering=' + pc.iceGatheringState + ' connection=' + (pc.connectionState || 'n/a');
    }

    function isCurrentPc(pc, pcId) {
        return X.state.callActive && X.state.peerConnection === pc && X.state.currentPcId === pcId && pc && pc.signalingState !== 'closed';
    }

    async function attachLocalVideoTrack(pc, stream) {
        if (!pc || !stream) return false;
        var track = X.media.getVideoTrack(stream);
        if (!track) return false;
        var sender = getVideoSender();
        if (sender) {
            if (sender.track === track) {
                X.log.peer('LOCAL_TRACK_ALREADY_ATTACHED');
                return true;
            }
            if (typeof sender.replaceTrack === 'function') {
                try {
                    await sender.replaceTrack(track);
                    X.log.peer('LOCAL_TRACK_ATTACHED_VIA_SENDER readyState=' + track.readyState + ' enabled=' + track.enabled);
                    return true;
                } catch (error) {
                    X.log.warn('Peer', 'REPLACE_TRACK_ATTACH_FAIL ' + error);
                    return false;
                }
            }
        }
        try {
            pc.addTrack(track, stream);
            var afterSenders = typeof pc.getSenders === 'function' ? pc.getSenders() : [];
            for (var j = 0; j < afterSenders.length; j++) {
                if (afterSenders[j].track === track) {
                    X.state.videoSender = afterSenders[j];
                    break;
                }
            }
            X.media.logVideoTrack('LOCAL_TRACK_ADDED', track);
            return true;
        } catch (error) {
            X.log.warn('Peer', 'ADD_VIDEO_TRACK_FAIL ' + error);
            return false;
        }
    }

    function ensureRecvOnlyVideo(pc) {
        if (!pc) return;
        if (X.state.videoTransceiver && X.state.videoSender) {
            X.log.peer('VIDEO_TRANSCEIVER_SENDRECV_NULL_TRACK');
            return;
        }
        if (typeof pc.getTransceivers === 'function') {
            var transceivers = pc.getTransceivers();
            for (var i = 0; i < transceivers.length; i++) {
                var transceiver = transceivers[i];
                var receiverTrack = transceiver.receiver && transceiver.receiver.track;
                if (receiverTrack && receiverTrack.kind === 'video') {
                    X.state.videoTransceiver = transceiver;
                    X.state.videoSender = transceiver.sender;
                    X.log.peer('VIDEO_TRANSCEIVER_SAVED track=' + (transceiver.sender && transceiver.sender.track ? 'live' : 'null'));
                    return;
                }
            }
        }
        if (typeof pc.addTransceiver === 'function') {
            try {
                var created = pc.addTransceiver('video', { direction: 'sendrecv' });
                X.state.videoTransceiver = created;
                X.state.videoSender = created.sender;
                X.log.peer('VIDEO_TRANSCEIVER_CREATED_SENDRECV track=null');
            } catch (error) {
                X.log.warn('Peer', 'ADD_TRANSCEIVER_FAIL ' + error);
            }
        }
    }

    function transceiverDirectionText(transceiver) {
        if (!transceiver) return 'none';
        var dir = transceiver.direction || 'n/a';
        var cur = transceiver.currentDirection || 'n/a';
        return dir + '/' + cur;
    }

    function getVideoTransceiver() {
        var pc = X.state.peerConnection;
        if (!pc || typeof pc.getTransceivers !== 'function') return null;
        if (X.state.videoTransceiver) {
            var transceivers = pc.getTransceivers();
            for (var i = 0; i < transceivers.length; i++) {
                if (transceivers[i] === X.state.videoTransceiver) {
                    return X.state.videoTransceiver;
                }
            }
        }
        var found = null;
        var all = pc.getTransceivers();
        for (var j = 0; j < all.length; j++) {
            var t = all[j];
            var receiverTrack = t.receiver && t.receiver.track;
            if (receiverTrack && receiverTrack.kind === 'video') {
                found = t;
                break;
            }
            if (!found && t.sender && t.sender.track && t.sender.track.kind === 'video') {
                found = t;
            }
        }
        if (found) {
            X.state.videoTransceiver = found;
            if (found.sender) X.state.videoSender = found.sender;
        }
        return found;
    }

    async function ensureVideoSenderTrack(track) {
        var transceiver = getVideoTransceiver();
        var sender = getVideoSender();
        if (!sender && transceiver && transceiver.sender) {
            sender = transceiver.sender;
            X.state.videoSender = sender;
        }
        if (!sender) {
            X.log.warn('Peer', 'ENSURE_VIDEO_SENDER_FAIL sender=null');
            return false;
        }
        if (transceiver && transceiver.direction !== 'sendrecv') {
            transceiver.direction = 'sendrecv';
            X.log.peer('TRANSCEIVER_DIRECTION_SET sendrecv');
        }
        X.state.videoSender = sender;
        if (sender.track === track) {
            X.log.peer('ENSURE_VIDEO_SENDER_OK alreadyBound direction=' + transceiverDirectionText(transceiver) + ' trackState=' + (track ? track.readyState : 'null'));
            return true;
        }
        if (!track) {
            X.log.warn('Peer', 'ENSURE_VIDEO_SENDER_FAIL track=null');
            return false;
        }
        if (typeof sender.replaceTrack !== 'function') {
            X.log.warn('Peer', 'ENSURE_VIDEO_SENDER_UNSUPPORTED');
            return false;
        }
        try {
            await sender.replaceTrack(track);
            var bound = sender.track === track;
            X.log.peer('ENSURE_VIDEO_SENDER_OK replaced direction=' + transceiverDirectionText(transceiver) + ' trackState=' + track.readyState + ' enabled=' + track.enabled + ' bound=' + (bound ? 'yes' : 'no'));
            return bound;
        } catch (error) {
            X.log.warn('Peer', 'ENSURE_VIDEO_SENDER_REPLACE_FAIL ' + error);
            return false;
        }
    }

    function verifyVideoTx(label) {
        var sender = getVideoSender();
        var transceiver = getVideoTransceiver();
        var currentTrack = X.media.getVideoTrack(X.state.localStream);
        var senderTrack = sender ? sender.track : null;
        var same = !!(currentTrack && senderTrack === currentTrack);
        X.log.peer('VIDEO_TX_VERIFY label=' + label + ' sender=' + (sender ? 'yes' : 'null') + ' senderTrack=' + (senderTrack ? senderTrack.readyState : 'null') + ' sameCurrentTrack=' + same + ' direction=' + transceiverDirectionText(transceiver) + ' localTrack=' + (currentTrack ? currentTrack.readyState : 'null') + ' cameraLastAction=' + X.state.cameraLastAction);
        return {
            sender: sender,
            transceiver: transceiver,
            senderTrack: senderTrack,
            sameCurrentTrack: same
        };
    }

    function attachRemotePreview(stream) {
        var remoteVideo = X.dom.remoteVideo;
        if (!remoteVideo) return;
        remoteVideo.srcObject = stream;
        X.hideEmpty();
        remoteVideo.onloadedmetadata = function () {
            X.log.peer('REMOTE_VIDEO_METADATA width=' + remoteVideo.videoWidth + ' height=' + remoteVideo.videoHeight + ' readyState=' + remoteVideo.readyState);
        };
        remoteVideo.onplaying = function () {
            X.log.peer('REMOTE_VIDEO_PLAYING width=' + remoteVideo.videoWidth + ' height=' + remoteVideo.videoHeight + ' readyState=' + remoteVideo.readyState);
        };
        remoteVideo.onresize = function () {
            X.log.peer('REMOTE_VIDEO_RESIZE width=' + remoteVideo.videoWidth + ' height=' + remoteVideo.videoHeight);
        };
        remoteVideo.onwaiting = function () {
            X.log.warn('Peer', 'REMOTE_VIDEO_WAITING');
        };
        remoteVideo.onstalled = function () {
            X.log.warn('Peer', 'REMOTE_VIDEO_STALLED');
        };
        remoteVideo.onerror = function () {
            X.log.warn('Peer', 'REMOTE_VIDEO_ELEMENT_ERROR');
        };
        try {
            var promise = remoteVideo.play();
            if (promise && typeof promise.catch === 'function') {
                promise.catch(function (error) {
                    X.log.warn('Peer', 'REMOTE_VIDEO_PLAY_FAIL ' + error);
                });
            }
        } catch (error) {
            X.log.warn('Peer', 'REMOTE_VIDEO_PLAY_FAIL ' + error);
        }
    }

    function setupVideoTransceiver(pc) {
        if (!pc) return;
        var stream = X.state.localStream;
        var track = X.media.getVideoTrack(stream);
        var transceiver = null;
        var sender = null;
        if (track && track.readyState === 'live' && typeof pc.addTrack === 'function') {
            try {
                pc.addTrack(track, stream);
                var senders = typeof pc.getSenders === 'function' ? pc.getSenders() : [];
                for (var j = 0; j < senders.length; j++) {
                    if (senders[j].track === track) {
                        sender = senders[j];
                        break;
                    }
                }
                if (sender && typeof pc.getTransceivers === 'function') {
                    var all = pc.getTransceivers();
                    for (var k = 0; k < all.length; k++) {
                        if (all[k].sender === sender) {
                            transceiver = all[k];
                            break;
                        }
                    }
                }
                if (transceiver) transceiver.direction = 'sendrecv';
                X.log.peer('VIDEO_TRANSCEIVER_CREATED_ADDTACK track=live');
            } catch (error) {
                X.log.warn('Peer', 'ADD_TRACK_TRANSCEIVER_FAIL ' + error);
                sender = null;
                transceiver = null;
            }
        }
        if (!transceiver && typeof pc.addTransceiver === 'function') {
            try {
                transceiver = pc.addTransceiver('video', { direction: 'sendrecv' });
                sender = transceiver.sender;
                if (track && track.readyState === 'live' && sender && typeof sender.replaceTrack === 'function') {
                    sender.replaceTrack(track);
                }
                X.log.peer('VIDEO_TRANSCEIVER_CREATED_SENDRECV track=' + (track && track.readyState === 'live' ? 'live' : 'null'));
            } catch (error) {
                X.log.warn('Peer', 'ADD_TRANSCEIVER_FAIL ' + error);
                return;
            }
        }
        X.state.videoTransceiver = transceiver;
        X.state.videoSender = sender || (transceiver ? transceiver.sender : null);
        X.log.peer('VIDEO_SENDER_SAVED track=' + (X.state.videoSender && X.state.videoSender.track ? 'live' : 'null'));
    }
    function createPeerConnection() {
        if (X.state.peerConnection && X.state.peerConnection.signalingState !== 'closed') {
            return X.state.peerConnection;
        }
        var turnServers = X.ice.getTurnIceServers();
        if (!turnServers.length) {
            X.log.warn('Peer', 'TURN_CONFIG_MISSING_CREATE_PC');
        }
        X.state.pcSeq++;
        X.state.currentPcId = X.state.pcSeq;
        var pcId = X.state.currentPcId;
        var pc = new RTCPeerConnection({
            iceServers: turnServers,
            iceTransportPolicy: 'relay',
            sdpSemantics: 'unified-plan'
        });
        X.state.peerConnection = pc;
        X.state.videoTransceiver = null;
        X.state.videoSender = null;
        X.log.peer('CREATE_PC pcId=#' + pcId + ' video=true audio=false icePolicy=relay hasTurn=' + (turnServers.length > 0) + ' turnOnly=true canvasProbe=local');
        X.log.peer('PC_STATE pcId=#' + pcId + ' ' + pcState(pc));
        setupVideoTransceiver(pc);

        pc.onsignalingstatechange = function () {
            if (!isCurrentPc(pc, pcId)) return;
            X.log.peer('PC_STATE pcId=#' + pcId + ' ' + pcState(pc));
        };

        pc.onicegatheringstatechange = function () {
            if (!isCurrentPc(pc, pcId)) return;
            X.log.peer('PC_STATE pcId=#' + pcId + ' ' + pcState(pc));
            if (pc.iceGatheringState === 'complete') {
                X.log.peer('ICE_GATHERING_COMPLETE pcId=#' + pcId);
                X.stats.dumpStats('gathering-complete');
            }
        };

        pc.oniceconnectionstatechange = function () {
            if (!isCurrentPc(pc, pcId)) return;
            X.log.peer('PC_STATE pcId=#' + pcId + ' ' + pcState(pc));
            if (pc.iceConnectionState === 'connected' || pc.iceConnectionState === 'completed') {
                X.log.peer('ICE_CONNECTED pcId=#' + pcId);
                X.bridge.reportState('connected');
                X.stats.startStats();
                X.stats.dumpStats('ice-connected');
            }
            if (pc.iceConnectionState === 'failed') {
                X.log.warn('Peer', 'ICE_FAILED pcId=#' + pcId);
                X.stats.dumpStats('ice-failed');
            }
            if (pc.iceConnectionState === 'disconnected') {
                X.log.warn('Peer', 'ICE_DISCONNECTED pcId=#' + pcId);
                X.stats.dumpStats('ice-disconnected');
            }
        };

        pc.onconnectionstatechange = function () {
            if (!isCurrentPc(pc, pcId)) return;
            X.log.peer('PC_STATE pcId=#' + pcId + ' ' + pcState(pc));
            if (pc.connectionState === 'connected') {
                X.log.peer('DTLS_CONNECTION_CONNECTED pcId=#' + pcId);
                X.stats.dumpStats('dtls-connected');
            }
            if (pc.connectionState === 'failed') {
                X.log.warn('Peer', 'CONNECTION_FAILED pcId=#' + pcId);
                X.stats.dumpStats('connection-failed');
            }
        };

        pc.onicecandidate = function (event) {
            if (!isCurrentPc(pc, pcId)) return;
            if (!event.candidate) {
                X.log.peer('ICE_GATHERING_COMPLETE pcId=#' + pcId);
                return;
            }
            var line = event.candidate.candidate;
            var parsed = X.ice.parseCandidate(line);
            X.log.peer('CAND_LOCAL ' + X.ice.candidateText(line));
            if (parsed.type === 'relay') {
                X.log.peer('TURN_RELAY_CANDIDATE_FOUND ' + X.ice.candidateText(line));
            }
            X.bridge.sendSignal({
                type: 'ice_candidate',
                candidate: line,
                sdpMid: event.candidate.sdpMid,
                sdpMLineIndex: event.candidate.sdpMLineIndex
            });
        };

        pc.ontrack = function (event) {
            if (!isCurrentPc(pc, pcId)) return;
            if (!event.track) return;
            X.log.peer('REMOTE_TRACK kind=' + event.track.kind + ' id=' + (event.track.id || '?') + ' readyState=' + event.track.readyState);
            if (event.track.kind !== 'video') return;
            var stream = null;
            if (event.streams && event.streams.length) {
                stream = event.streams[0];
            } else {
                try {
                    stream = new MediaStream([event.track]);
                } catch (error) {
                    X.log.warn('Peer', 'REMOTE_STREAM_CREATE_FAIL ' + error);
                }
            }
            if (stream) {
                attachRemotePreview(stream);
                X.log.peer('REMOTE_VIDEO_ATTACHED');
            }
            event.track.onmute = function () {
                X.log.warn('Peer', 'REMOTE_VIDEO_TRACK_MUTED');
            };
            event.track.onunmute = function () {
                X.log.peer('REMOTE_VIDEO_TRACK_UNMUTED');
            };
            event.track.onended = function () {
                X.log.warn('Peer', 'REMOTE_VIDEO_TRACK_ENDED');
            };
        };

        return pc;
    }

    function closePc() {
        var pc = X.state.peerConnection;
        var pcId = X.state.currentPcId;
        X.state.peerConnection = null;
        X.state.currentPcId = 0;
        X.state.videoTransceiver = null;
        X.state.videoSender = null;
        if (!pc) return;
        try {
            X.log.peer('CLOSE_PC_BEGIN pcId=#' + pcId + ' ' + pcState(pc));
            pc.onicecandidate = null;
            pc.ontrack = null;
            pc.onsignalingstatechange = null;
            pc.onicegatheringstatechange = null;
            pc.oniceconnectionstatechange = null;
            pc.onconnectionstatechange = null;
            pc.close();
            X.log.peer('CLOSE_PC_DONE pcId=#' + pcId);
        } catch (error) {
            X.log.warn('Peer', 'CLOSE_PC_ERROR ' + error);
        }
    }

    function getVideoSender() {
        var pc = X.state.peerConnection;
        if (!pc || typeof pc.getSenders !== 'function') return null;
        var senders = pc.getSenders();
        if (X.state.videoSender) {
            for (var i = 0; i < senders.length; i++) {
                if (senders[i] === X.state.videoSender) {
                    return X.state.videoSender;
                }
            }
        }
        for (var j = 0; j < senders.length; j++) {
            if (senders[j].track && senders[j].track.kind === 'video') {
                X.state.videoSender = senders[j];
                return senders[j];
            }
        }
        if (typeof pc.getTransceivers === 'function') {
            var transceivers = pc.getTransceivers();
            for (var k = 0; k < transceivers.length; k++) {
                var transceiver = transceivers[k];
                var receiverTrack = transceiver.receiver && transceiver.receiver.track;
                if (receiverTrack && receiverTrack.kind === 'video') {
                    X.state.videoTransceiver = transceiver;
                    X.state.videoSender = transceiver.sender;
                    return transceiver.sender;
                }
            }
        }
        return null;
    }

    function replaceVideoTrack(track) {
        var sender = getVideoSender();
        if (!sender) {
            X.log.warn('Peer', 'VIDEO_SENDER_REPLACE_FAIL sender=null');
            return Promise.reject(new Error('no video sender'));
        }
        if (!track) {
            X.log.warn('Peer', 'VIDEO_SENDER_REPLACE_FAIL track=null');
            return Promise.reject(new Error('no video track'));
        }
        X.log.peer('VIDEO_SENDER_REPLACE_BEGIN readyState=' + track.readyState + ' enabled=' + track.enabled);
        if (typeof sender.replaceTrack !== 'function') {
            X.log.warn('Peer', 'VIDEO_SENDER_REPLACE_UNSUPPORTED');
            return Promise.resolve(true);
        }
        return sender.replaceTrack(track).then(function () {
            X.log.peer('VIDEO_SENDER_REPLACE_OK readyState=' + track.readyState);
            return true;
        }).catch(function (error) {
            X.log.warn('Peer', 'VIDEO_SENDER_REPLACE_FAIL ' + error);
            throw error;
        });
    }

    function getVideoOutFrames(callback) {
        var pc = X.state.peerConnection;
        if (!pc || typeof pc.getStats !== 'function') {
            if (callback) callback(-1);
            return;
        }
        pc.getStats().then(function (stats) {
            var frames = -1;
            stats.forEach(function (report) {
                if (report.type === 'outbound-rtp' && report.kind === 'video') {
                    var f = report.framesEncoded !== undefined ? report.framesEncoded : report.framesSent;
                    if (f !== undefined) frames = f;
                }
            });
            if (callback) callback(frames);
        }).catch(function () {
            if (callback) callback(-1);
        });
    }
    X.peer = {
        pcState: pcState,
        isCurrentPc: isCurrentPc,
        createPeerConnection: createPeerConnection,
        attachLocalVideoTrack: attachLocalVideoTrack,
        ensureRecvOnlyVideo: ensureRecvOnlyVideo,
        attachRemotePreview: attachRemotePreview,
        getVideoSender: getVideoSender,
        getVideoTransceiver: getVideoTransceiver,
        ensureVideoSenderTrack: ensureVideoSenderTrack,
        verifyVideoTx: verifyVideoTx,
        transceiverDirectionText: transceiverDirectionText,
        replaceVideoTrack: replaceVideoTrack,
        getVideoOutFrames: getVideoOutFrames,
        closePc: closePc
    };

    X.log.peer('loaded');
}(window.Xiaofu = window.Xiaofu || {}));

