(function (X) {
    'use strict';

    var initialized = false;

    X.bridge = {
        init: function () {
            if (initialized) return;
            if (typeof QWebChannel === 'undefined' || typeof qt === 'undefined' || !qt.webChannelTransport) {
                X.log.warn('Bridge', 'QWEBCHANNEL_UNAVAILABLE');
                return;
            }
            initialized = true;
            X.log.bridge('QWEBCHANNEL_INIT_BEGIN');
            new QWebChannel(qt.webChannelTransport, function (channel) {
                X.state.bridge = channel.objects.webRtcBridge;
                X.log.bridge('QWEBCHANNEL_INIT_OK');
                X.bridge.reportState('ready');
            });
        },
        reportState: function (value) {
            X.log.bridge('REPORT_STATE state=' + value);
            if (X.state.bridge && typeof X.state.bridge.reportCallState === 'function') {
                X.state.bridge.reportCallState(value);
            }
        },
        reportPreviewReady: function (settings) {
            if (X.state.bridge && typeof X.state.bridge.reportPreviewReady === 'function') {
                X.state.bridge.reportPreviewReady(settings || {});
            }
        },
        sendSignal: function (signal) {
            if (!X.state.bridge || typeof X.state.bridge.reportOutgoingSignal !== 'function') {
                X.log.warn('Bridge', 'SIGNAL_SEND_FAIL bridge unavailable');
                return;
            }
            X.log.bridge('SIGNAL_SEND type=' + signal.type);
            X.state.bridge.reportOutgoingSignal(signal);
        }
    };

    X.log.bridge('loaded');

    X.bridge.init();
}(window.Xiaofu = window.Xiaofu || {}));
