(function (X) {
    'use strict';

    function eachReport(reports, callback) {
        if (reports && typeof reports.forEach === 'function') {
            reports.forEach(callback);
        }
    }

    function statsCandidateDescription(candidate) {
        if (!candidate) return 'n/a';
        return (candidate.candidateType || '?') + ':' + (candidate.address || candidate.ip || '?') + ':' + (candidate.port || '?') + '/' + (candidate.protocol || '?');
    }

    function codecName(codecs, codecId) {
        if (!codecId || !codecs[codecId]) return 'n/a';
        var codec = codecs[codecId];
        return codec.mimeType || codec.name || codec.codec || 'n/a';
    }

    function dumpStats(reason) {
        var pc = X.state.peerConnection;
        if (!pc || typeof pc.getStats !== 'function') return;
        pc.getStats().then(function (reports) {
            var candidates = {};
            var pairs = {};
            var codecs = {};
            var selectedPairId = '';
            eachReport(reports, function (report) {
                if (report.type === 'local-candidate' || report.type === 'remote-candidate') {
                    candidates[report.id] = report;
                }
                if (report.type === 'candidate-pair') {
                    pairs[report.id] = report;
                }
                if (report.type === 'codec') {
                    codecs[report.id] = report;
                }
                if (report.type === 'transport' && report.selectedCandidatePairId) {
                    selectedPairId = report.selectedCandidatePairId;
                }
            });
            eachReport(reports, function (report) {
                if (report.type === 'transport') {
                    var dtls = report.dtlsState || 'n/a';
                    X.log.stats('STATS_TRANSPORT reason=' + reason + ' ice=' + (report.iceState || pc.iceConnectionState || 'n/a') + ' dtls=' + dtls + ' pair=' + (report.selectedCandidatePairId || 'n/a'));
                    if (dtls === 'connected') {
                        X.log.stats('DTLS_CONNECTED_STATS');
                    }
                }
                if (report.type === 'candidate-pair' && (report.id === selectedPairId || report.selected || report.nominated || report.state === 'succeeded')) {
                    var local = candidates[report.localCandidateId];
                    var remote = candidates[report.remoteCandidateId];
                    X.log.stats('STATS_PAIR reason=' + reason + ' state=' + (report.state || '?') + ' nominated=' + !!report.nominated + ' selected=' + (report.id === selectedPairId || !!report.selected) + ' local=' + statsCandidateDescription(local) + ' remote=' + statsCandidateDescription(remote) + ' sent=' + (report.bytesSent || 0) + ' recv=' + (report.bytesReceived || 0));
                }
                if (report.type === 'outbound-rtp' && (report.kind === 'video' || report.mediaType === 'video')) {
                    X.log.stats('VIDEO_OUT reason=' + reason + ' codec=' + codecName(codecs, report.codecId) + ' packets=' + (report.packetsSent || 0) + ' bytes=' + (report.bytesSent || 0) + ' framesEncoded=' + (report.framesEncoded || report.framesSent || 0) + ' width=' + (report.frameWidth || '?') + ' height=' + (report.frameHeight || '?') + ' fps=' + (report.framesPerSecond || '?'));
                }
                if (report.type === 'inbound-rtp' && (report.kind === 'video' || report.mediaType === 'video')) {
                    X.log.stats('VIDEO_IN reason=' + reason + ' codec=' + codecName(codecs, report.codecId) + ' packets=' + (report.packetsReceived || 0) + ' lost=' + (report.packetsLost || 0) + ' bytes=' + (report.bytesReceived || 0) + ' framesDecoded=' + (report.framesDecoded || report.framesReceived || 0) + ' framesDropped=' + (report.framesDropped || 0) + ' width=' + (report.frameWidth || '?') + ' height=' + (report.frameHeight || '?') + ' fps=' + (report.framesPerSecond || '?'));
                }
            });
            if (selectedPairId && pairs[selectedPairId]) {
                var selected = pairs[selectedPairId];
                X.log.stats('STATS_SELECTED_PAIR reason=' + reason + ' local=' + statsCandidateDescription(candidates[selected.localCandidateId]) + ' remote=' + statsCandidateDescription(candidates[selected.remoteCandidateId]));
            }
            X.log.stats('PROBE_STATS reason=' + reason + ' frames=' + X.state.probeFrames + ' errors=' + X.state.probeErrors + ' hash=' + X.state.probeLastHash + ' source=' + X.state.probeLastWidth + 'x' + X.state.probeLastHeight + ' probeActive=' + X.state.probeActive);
        }).catch(function (error) {
            X.log.warn('Stats', 'GET_STATS_FAIL ' + error);
        });
    }

    function startStats() {
        if (X.state.statsTimer) return;
        X.state.statsCount = 0;
        dumpStats('start');
        X.state.statsTimer = setInterval(function () {
            X.state.statsCount++;
            dumpStats('t+' + (X.state.statsCount * 3) + 's');
            if (X.state.statsCount >= X.state.maxStatsCount) {
                stopStats();
            }
        }, 3000);
    }

    function stopStats() {
        if (!X.state.statsTimer) return;
        clearInterval(X.state.statsTimer);
        X.state.statsTimer = null;
    }

    function setDiagMaxDumps(value) {
        var n = Number(value);
        if (isFinite(n) && n > 0) {
            X.state.maxStatsCount = Math.floor(n);
        }
    }

    X.stats = {
        dumpStats: dumpStats,
        startStats: startStats,
        stopStats: stopStats,
        setDiagMaxDumps: setDiagMaxDumps
    };

    X.log.stats('loaded');
}(window.Xiaofu = window.Xiaofu || {}));

