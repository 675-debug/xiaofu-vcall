(function (X) {
    'use strict';

    var supported = false;

    function detect() {
        supported = !!(navigator.mediaDevices && typeof navigator.mediaDevices.getDisplayMedia === 'function');
        X.state.screen.supported = supported;
        X.log.screen('CAPABILITY getDisplayMedia=' + supported);
    }

    function isSupported() {
        return supported;
    }

    function start() {
        if (!supported) {
            X.log.screen('START_REJECTED unsupported');
            return Promise.reject(new Error('getDisplayMedia unsupported'));
        }
        X.log.screen('START_REQUESTED foundation-only');
        return Promise.resolve(null);
    }

    function stop() {
        X.state.screen.active = false;
        if (X.state.screen.track) {
            try {
                X.state.screen.track.stop();
            } catch (error) {
            }
            X.state.screen.track = null;
        }
        if (X.state.screen.stream) {
            X.state.screen.stream = null;
        }
        X.log.screen('STOP');
    }

    function getState() {
        return {
            supported: supported,
            active: X.state.screen.active
        };
    }

    function init() {
        detect();
    }

    X.screen = {
        init: init,
        isSupported: isSupported,
        start: start,
        stop: stop,
        getState: getState
    };

    X.log.screen('loaded');
}(window.Xiaofu = window.Xiaofu || {}));
