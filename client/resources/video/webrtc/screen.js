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

    function restoreCamera() {
        var cameraTrack = X.media.getVideoTrack ? X.media.getVideoTrack(X.state.localStream) : null;
        if (cameraTrack && cameraTrack.readyState === 'live' && X.peer && X.peer.replaceVideoTrack) {
            X.peer.replaceVideoTrack(cameraTrack).catch(function (error) {
                X.log.warn('Screen', 'RESTORE_CAMERA_FAIL ' + error);
            });
        }
    }

    function start() {
        if (!supported) {
            X.log.screen('START_REJECTED unsupported');
            return Promise.reject(new Error('getDisplayMedia unsupported'));
        }
        if (X.state.screen.active) return Promise.resolve(X.state.screen.stream);
        X.log.screen('START_REQUESTED');
        return navigator.mediaDevices.getDisplayMedia({ video: true }).then(function (stream) {
            var track = stream.getVideoTracks ? stream.getVideoTracks()[0] : null;
            if (!track) {
                stream.getTracks().forEach(function (t) {
                    try { t.stop(); } catch (error) {}
                });
                throw new Error('no screen track');
            }
            X.state.screen.stream = stream;
            X.state.screen.track = track;
            return X.peer.replaceVideoTrack(track).then(function () {
                X.state.screen.active = true;
                track.onended = function () {
                    X.log.screen('SCREEN_TRACK_ENDED restore camera');
                    stop();
                    if (X.ui && X.ui.setScreenShareState) X.ui.setScreenShareState(false);
                };
                X.log.screen('START_OK track=' + track.readyState);
                if (X.ui && X.ui.setScreenShareState) X.ui.setScreenShareState(true);
                return stream;
            }).catch(function (error) {
                stream.getTracks().forEach(function (t) {
                    try { t.stop(); } catch (error2) {}
                });
                X.state.screen.stream = null;
                X.state.screen.track = null;
                X.log.warn('Screen', 'REPLACE_SCREEN_TRACK_FAIL ' + error);
                throw error;
            });
        }).catch(function (error) {
            X.log.warn('Screen', 'START_FAIL ' + error);
            throw error;
        });
    }

    function stop() {
        var wasActive = X.state.screen.active;
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
        if (wasActive) restoreCamera();
        if (wasActive && X.ui && X.ui.setScreenShareState) X.ui.setScreenShareState(false);
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
