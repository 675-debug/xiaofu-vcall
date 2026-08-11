(function (X) {
    'use strict';

    var BUILD = '2026-08-08-r11-call-reliability-ux';

    X.core = {
        build: BUILD
    };

    X.dom = {
        localVideo: document.getElementById('local-video'),
        localPip: document.getElementById('local-pip'),
        remoteVideo: document.getElementById('remote-video'),
        emptyState: document.getElementById('empty-state'),
        callStage: document.getElementById('call-stage')
    };

    X.text = function (value) {
        if (value === undefined || value === null) return '';
        return String(value);
    };

    X.setEmpty = function (message) {
        if (!X.dom.emptyState) return;
        X.dom.emptyState.textContent = message;
        X.dom.emptyState.style.display = 'flex';
    };

    X.hideEmpty = function () {
        if (!X.dom.emptyState) return;
        X.dom.emptyState.style.display = 'none';
    };

    console.log('[Core] loaded build=' + BUILD);
    console.log('[Core] USER_AGENT ' + navigator.userAgent);
    console.log('[Core] PLATFORM ' + (navigator.platform || 'n/a'));
}(window.Xiaofu = window.Xiaofu || {}));
