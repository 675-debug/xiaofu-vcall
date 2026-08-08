(function (X) {
    'use strict';

    async function addCandidate(pc, candidate) {
        if (!pc || pc !== X.state.peerConnection || !X.state.callActive || pc.signalingState === 'closed') {
            X.log.warn('Signal', 'CAND_REJECT_NO_ACTIVE_PC');
            return;
        }
        if (!candidate || !candidate.candidate) return;
        if (!X.ice.candidateMatchesRemoteDescription(pc, candidate.candidate)) {
            X.log.warn('Signal', 'CAND_REJECT_UFRAG_MISMATCH ' + X.ice.candidateText(candidate.candidate));
            return;
        }
        X.log.signal('CAND_ADD_START ' + X.ice.candidateText(candidate.candidate));
        try {
            await pc.addIceCandidate(candidate);
            X.log.ice('CANDIDATE_ADD_OK');
        } catch (error) {
            X.log.warn('Signal', 'CAND_ADD_FAIL ' + error);
        }
    }

    async function flushCandidates(pc) {
        while (X.state.pendingCandidates.length && pc && pc.remoteDescription) {
            if (pc !== X.state.peerConnection || !X.state.callActive || pc.signalingState === 'closed') {
                X.state.pendingCandidates = [];
                return;
            }
            var candidate = X.state.pendingCandidates.shift();
            X.log.signal('CANDIDATE_FLUSH remaining=' + X.state.pendingCandidates.length);
            await addCandidate(pc, candidate);
        }
    }

    async function startOutgoingCall() {
        if (X.state.outgoingOfferStarted) {
            X.log.warn('Signal', 'OUTGOING_ALREADY_STARTED');
            return;
        }
        X.state.outgoingOfferStarted = true;
        X.state.callActive = true;
        var token = X.state.callToken;
        var stream = await X.media.startPreview();
        if (token !== X.state.callToken || !X.state.callActive) return;
        X.log.peer('ROLE caller');
        var pc = X.peer.createPeerConnection();
        var attached = await X.peer.attachLocalVideoTrack(pc, stream);
        if (!attached) {
            X.log.warn('Signal', 'OUTGOING_NO_CAMERA_RECVONLY');
            X.peer.ensureRecvOnlyVideo(pc);
        }
        var track = X.media.getVideoTrack(stream);
        await X.peer.ensureVideoSenderTrack(track);
        X.peer.verifyVideoTx('caller-before-offer');
        var offer = await pc.createOffer();
        if (token !== X.state.callToken || pc !== X.state.peerConnection || !X.state.callActive) return;
        await pc.setLocalDescription(offer);
        X.ice.sdpSummary('LOCAL_OFFER', offer.sdp);
        X.log.ice('LOCAL_OFFER_VIDEO_DIRECTION ' + X.ice.sdpVideoInfo(offer.sdp));
        X.peer.verifyVideoTx('caller-after-local-offer');
        X.log.signal('OFFER_CREATED chars=' + offer.sdp.length);
        X.bridge.sendSignal({ type: 'webrtc_offer', sdp: offer.sdp });
    }

    async function handleOffer(signal) {
        X.state.callActive = true;
        var token = X.state.callToken;
        X.log.peer('ROLE callee');
        X.log.peer('CALLEE_OFFER_RECEIVED chars=' + X.text(signal.sdp).length);
        var stream = await X.media.startPreview();
        if (token !== X.state.callToken || !X.state.callActive) return;
        var track = X.media.getVideoTrack(stream);
        if (track && track.readyState !== 'live') {
            X.log.warn('Signal', 'CALLEE_CAMERA_NOT_LIVE readyState=' + track.readyState + ' cameraLabel=' + X.state.cameraDeviceLabel);
        }
        var pc = X.peer.createPeerConnection();
        X.log.peer('CALLEE_TRACK_BIND_BEGIN track=' + (track ? track.id : 'null') + ' readyState=' + (track ? track.readyState : 'n/a') + ' enabled=' + (track ? track.enabled : 'n/a') + ' muted=' + (track ? track.muted : 'n/a'));
        var ensured = await X.peer.ensureVideoSenderTrack(track);
        if (ensured) {
            X.log.peer('CALLEE_TRACK_BIND_OK');
        } else {
            X.log.warn('Peer', 'CALLEE_TRACK_BIND_FAIL');
        }
        var before = X.peer.verifyVideoTx('callee-before-setremote');
        X.log.peer('BEFORE_CREATE_ANSWER cameraTrack=' + (track ? track.id : 'null') + ' readyState=' + (track ? track.readyState : 'n/a') + ' enabled=' + (track ? track.enabled : 'n/a') + ' muted=' + (track ? track.muted : 'n/a') + ' transceiverDirection=' + (before.transceiver ? (before.transceiver.direction || 'n/a') + '/' + (before.transceiver.currentDirection || 'n/a') : 'none') + ' senderTrack=' + (before.senderTrack ? before.senderTrack.id : 'null') + ' sameCurrentTrack=' + before.sameCurrentTrack + ' ensured=' + ensured);
        await pc.setRemoteDescription({ type: 'offer', sdp: signal.sdp });
        X.log.ice('REMOTE_OFFER_VIDEO_DIRECTION ' + X.ice.sdpVideoInfo(signal.sdp));
        await flushCandidates(pc);
        var answer = await pc.createAnswer();
        var after = X.peer.verifyVideoTx('callee-after-createanswer');
        var dir = X.ice.sdpVideoDirection(answer.sdp);
        X.log.peer('AFTER_CREATE_ANSWER direction=' + dir + ' senderTrack=' + (after.senderTrack ? after.senderTrack.id : 'null') + ' sameCurrentTrack=' + after.sameCurrentTrack + ' transceiver=' + (after.transceiver ? (after.transceiver.direction || 'n/a') + '/' + (after.transceiver.currentDirection || 'n/a') : 'none'));
        if (dir !== 'sendrecv' && track && track.readyState === 'live') {
            X.log.warn('Peer', 'ANSWER_DIRECTION_WRONG expected=sendrecv actual=' + dir + ' senderTrack=' + (after.senderTrack ? after.senderTrack.id : 'null'));
        }
        await pc.setLocalDescription(answer);
        var afterLocal = X.peer.verifyVideoTx('callee-after-set-local-answer');
        X.log.peer('AFTER_SET_LOCAL_ANSWER senderTrack=' + (afterLocal.senderTrack ? afterLocal.senderTrack.id : 'null') + ' currentDirection=' + (afterLocal.transceiver ? (afterLocal.transceiver.currentDirection || 'n/a') : 'none'));
        X.ice.sdpSummary('LOCAL_ANSWER', answer.sdp);
        X.log.ice('LOCAL_ANSWER_VIDEO_DIRECTION ' + X.ice.sdpVideoInfo(answer.sdp));
        X.log.ice('LOCAL_ANSWER_MSID=' + (X.ice.sdpHasMsid(answer.sdp) ? 'yes' : 'no'));
        X.log.ice('LOCAL_ANSWER_SSRC=' + X.ice.sdpSsrcCount(answer.sdp));
        X.log.signal('ANSWER_CREATED chars=' + answer.sdp.length);
        X.bridge.sendSignal({ type: 'webrtc_answer', sdp: answer.sdp });
    }

    async function handleAnswer(signal) {
        var pc = X.state.peerConnection;
        if (!X.state.callActive || !X.state.outgoingOfferStarted || !pc || pc.signalingState === 'closed') {
            X.log.warn('Signal', 'ANSWER_REJECT_NO_ACTIVE_OFFER');
            return;
        }
        X.ice.sdpSummary('REMOTE_ANSWER', signal.sdp);
        X.log.ice('REMOTE_ANSWER_VIDEO_DIRECTION ' + X.ice.sdpVideoInfo(signal.sdp));
        await pc.setRemoteDescription({ type: 'answer', sdp: signal.sdp });
        X.log.signal('ANSWER_APPLIED');
        X.peer.verifyVideoTx('caller-after-remote-answer');
        await flushCandidates(pc);
    }

    async function handleCandidate(signal) {
        var candidate = {
            candidate: signal.candidate,
            sdpMid: signal.sdpMid,
            sdpMLineIndex: signal.sdpMLineIndex
        };
        if (!X.state.callActive) {
            X.log.warn('Signal', 'CAND_REJECT_NO_SESSION ' + X.ice.candidateText(signal.candidate));
            return;
        }
        if (!X.state.peerConnection) {
            X.state.pendingCandidates.push(candidate);
            X.log.signal('CANDIDATE_BUFFERED_NO_PC size=' + X.state.pendingCandidates.length);
            return;
        }
        var pc = X.state.peerConnection;
        X.log.signal('CAND_RECV remoteDescription=' + (pc.remoteDescription ? 'yes' : 'no') + ' ' + X.ice.candidateText(signal.candidate));
        if (pc.remoteDescription) {
            await addCandidate(pc, candidate);
            return;
        }
        X.state.pendingCandidates.push(candidate);
        X.log.signal('CAND_BUFFER size=' + X.state.pendingCandidates.length);
    }

    async function applyRemoteSignal(signal) {
        if (!signal) return;
        var type = signal.type;
        if (type === 'ice_candidate' || type === 'webrtc_ice') {
            await handleCandidate(signal);
            return;
        }
        if (type === 'webrtc_offer') {
            await handleOffer(signal);
            return;
        }
        if (type === 'webrtc_answer') {
            await handleAnswer(signal);
            return;
        }
        X.log.warn('Signal', 'UNKNOWN_REMOTE_SIGNAL type=' + type);
    }

    X.signal = {
        startOutgoingCall: startOutgoingCall,
        applyRemoteSignal: applyRemoteSignal,
        flushCandidates: flushCandidates
    };

    X.log.signal('loaded');
}(window.Xiaofu = window.Xiaofu || {}));
