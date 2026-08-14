(function (X) {
    'use strict';

    function normalizeUrls(server) {
        if (!server) return [];
        if (Array.isArray(server.urls)) return server.urls.slice();
        if (server.urls) return [server.urls];
        if (server.url) return [server.url];
        return [];
    }

    function isTurnUrl(url) {
        return /^turns?:/i.test(X.text(url));
    }

    function getTurnIceServers() {
        var result = [];
        X.state.iceServers.forEach(function (server) {
            var urls = normalizeUrls(server).filter(isTurnUrl);
            if (!urls.length) return;
            var item = { urls: urls.length === 1 ? urls[0] : urls };
            if (server.username !== undefined) item.username = server.username;
            if (server.credential !== undefined) item.credential = server.credential;
            if (server.credentialType !== undefined) item.credentialType = server.credentialType;
            result.push(item);
        });
        return result;
    }

    function hasTurnServer() {
        return getTurnIceServers().length > 0;
    }

    function safeServer(server) {
        var output = { urls: server.urls || server.url };
        if (server.username) output.username = '<set>';
        if (server.credential) output.credential = '<set>';
        return output;
    }

    function safeIceServers(servers) {
        return (servers || []).map(safeServer);
    }

    function iceServerTypes() {
        var types = [];
        X.state.iceServers.forEach(function (server) {
            var urls = normalizeUrls(server);
            urls.forEach(function (url) {
                var t = /^turns?:/i.test(X.text(url)) ? 'turn' : (/^stuns?:/i.test(X.text(url)) ? 'stun' : 'other');
                if (types.indexOf(t) < 0) types.push(t);
            });
        });
        return types;
    }

    function setIceServers(servers) {
        X.state.iceServers = Array.isArray(servers) ? servers.slice() : [];
        X.log.ice('ICE_SERVERS ' + JSON.stringify(safeIceServers(X.state.iceServers)));
        X.log.ice('ICE_SERVER_TYPES ' + iceServerTypes().join(','));
        var turnServers = getTurnIceServers();
        if (!turnServers.length) {
            if (X.state.iceTransportPolicy === 'relay') {
                X.log.warn('ICE', 'TURN_CONFIG_MISSING relay policy requires TURN');
                X.setEmpty('TURN unavailable');
            } else {
                X.log.warn('ICE', 'TURN_FALLBACK_UNAVAILABLE no turn server, STUN/P2P only');
            }
            return;
        }
        X.log.ice('TURN_CONFIG_OK count=' + turnServers.length);
        X.log.ice('TURN_ONLY_SERVERS ' + JSON.stringify(safeIceServers(turnServers)));
    }

    function setIcePolicy(policy) {
        var p = X.text(policy).trim().toLowerCase();
        if (p === 'relay') {
            X.state.iceTransportPolicy = 'relay';
        } else {
            X.state.iceTransportPolicy = 'all';
        }
        X.log.ice('ICE_POLICY=' + X.state.iceTransportPolicy);
    }

    function parseCandidate(candidateLine) {
        var raw = X.text(candidateLine);
        var body = raw.indexOf('candidate:') === 0 ? raw.substring(10) : raw;
        var parts = body.trim().split(/\s+/);
        var result = {
            raw: raw,
            foundation: '',
            component: '',
            protocol: '',
            priority: '',
            address: '',
            port: '',
            type: '',
            relatedAddress: '',
            relatedPort: '',
            ufrag: ''
        };
        if (parts.length < 6) return result;
        result.foundation = parts[0] || '';
        result.component = parts[1] || '';
        result.protocol = parts[2] || '';
        result.priority = parts[3] || '';
        result.address = parts[4] || '';
        result.port = parts[5] || '';
        for (var i = 6; i + 1 < parts.length; i++) {
            if (parts[i] === 'typ') result.type = parts[i + 1];
            if (parts[i] === 'raddr') result.relatedAddress = parts[i + 1];
            if (parts[i] === 'rport') result.relatedPort = parts[i + 1];
            if (parts[i] === 'ufrag') result.ufrag = parts[i + 1];
        }
        return result;
    }

    function candidateText(candidateLine) {
        var c = parseCandidate(candidateLine);
        return 'type=' + (c.type || '?') + ' address=' + (c.address || '?') + ':' + (c.port || '?') + ' proto=' + (c.protocol || '?') + ' ufrag=' + (c.ufrag || '?');
    }

    function sdpUfrag(sdp) {
        var match = /(?:^|\r?\n)a=ice-ufrag:([^\r\n]+)/.exec(X.text(sdp));
        return match ? match[1].trim() : '';
    }

    function videoSection(sdp) {
        var value = X.text(sdp);
        var index = value.search(/(?:^|\r?\n)m=video\s+/m);
        if (index < 0) return '';
        var start = value.indexOf('m=video', index);
        var lf = value.indexOf('\nm=', start + 1);
        var cr = value.indexOf('\rm=', start + 1);
        var nextIndex = -1;
        if (lf >= 0 && cr >= 0) nextIndex = Math.min(lf, cr);
        else if (lf >= 0) nextIndex = lf;
        else if (cr >= 0) nextIndex = cr;
        return nextIndex >= 0 ? value.substring(start, nextIndex) : value.substring(start);
    }

    function sdpVideoDirection(sdp) {
        var section = videoSection(sdp);
        if (!section) return 'none';
        if (/(?:^|\r?\n)a=sendrecv(?:\r?\n|$)/m.test(section)) return 'sendrecv';
        if (/(?:^|\r?\n)a=sendonly(?:\r?\n|$)/m.test(section)) return 'sendonly';
        if (/(?:^|\r?\n)a=recvonly(?:\r?\n|$)/m.test(section)) return 'recvonly';
        if (/(?:^|\r?\n)a=inactive(?:\r?\n|$)/m.test(section)) return 'inactive';
        return 'implicit-sendrecv';
    }

    function sdpVideoInfo(sdp) {
        var value = X.text(sdp);
        var mCount = (value.match(/(?:^|\r?\n)m=/gm) || []).length;
        var section = videoSection(sdp);
        if (!section) {
            return 'mLines=' + mCount + ' videoSection=none direction=none payloadType=? msid=no ssrc=0';
        }
        var direction = sdpVideoDirection(sdp);
        var msid = /(?:^|\r?\n)a=msid:[^\r\n]+/m.test(section);
        var ssrcCount = (section.match(/(?:^|\r?\n)a=ssrc:\d+/gm) || []).length;
        var ptMatch = /m=video\s+\d+\s+[\w/]+\s+(\d+)/.exec(section);
        var pt = ptMatch ? ptMatch[1] : '?';
        return 'mLines=' + mCount + ' direction=' + direction + ' payloadType=' + pt + ' msid=' + (msid ? 'yes' : 'no') + ' ssrc=' + ssrcCount;
    }

    function sdpHasMsid(sdp) {
        var section = videoSection(sdp);
        if (!section) return false;
        return /(?:^|\r?\n)a=msid:[^\r\n]+/m.test(section);
    }

    function sdpSsrcCount(sdp) {
        var section = videoSection(sdp);
        if (!section) return 0;
        return (section.match(/(?:^|\r?\n)a=ssrc:\d+/gm) || []).length;
    }

    function sdpSummary(label, sdp) {
        X.log.ice(label + ' SDP_SUMMARY videoDirection=' + sdpVideoDirection(sdp) + ' iceUfrag=' + (sdpUfrag(sdp) || 'none'));
    }

    function remoteDescriptionUfrag(pc) {
        if (!pc || !pc.remoteDescription || !pc.remoteDescription.sdp) return '';
        return sdpUfrag(pc.remoteDescription.sdp);
    }

    function candidateMatchesRemoteDescription(pc, candidateLine) {
        var candidate = parseCandidate(candidateLine);
        var expected = remoteDescriptionUfrag(pc);
        if (!candidate.ufrag) return true;
        if (!expected) return true;
        return candidate.ufrag === expected;
    }

    X.ice = {
        normalizeUrls: normalizeUrls,
        isTurnUrl: isTurnUrl,
        getTurnIceServers: getTurnIceServers,
        iceServerTypes: iceServerTypes,
        hasTurnServer: hasTurnServer,
        safeServer: safeServer,
        safeIceServers: safeIceServers,
        setIceServers: setIceServers,
        setIcePolicy: setIcePolicy,
        parseCandidate: parseCandidate,
        candidateText: candidateText,
        sdpUfrag: sdpUfrag,
        videoSection: videoSection,
        sdpVideoDirection: sdpVideoDirection,
        sdpVideoInfo: sdpVideoInfo,
        sdpHasMsid: sdpHasMsid,
        sdpSsrcCount: sdpSsrcCount,
        sdpSummary: sdpSummary,
        remoteDescriptionUfrag: remoteDescriptionUfrag,
        candidateMatchesRemoteDescription: candidateMatchesRemoteDescription
    };

    X.log.ice('loaded');
}(window.Xiaofu = window.Xiaofu || {}));
