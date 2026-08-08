(function (X) {
    'use strict';

    var cameraProfiles = {
        qvga: { name: 'qvga', label: '320x240', uiLabel: '流畅', width: 320, height: 240 },
        vga: { name: 'vga', label: '640x480', uiLabel: '标准', width: 640, height: 480 },
        hd: { name: 'hd', label: '1280x720', uiLabel: '高清', width: 1280, height: 720 },
        fhd: { name: 'fhd', label: '1920x1080', uiLabel: '1080P', width: 1920, height: 1080 }
    };

    function stopTracks(stream) {
        if (!stream) return;
        stream.getTracks().forEach(function (track) {
            try {
                track.stop();
            } catch (error) {
            }
        });
    }

    function getVideoTrack(stream) {
        if (!stream) return null;
        var tracks = stream.getVideoTracks();
        return tracks.length ? tracks[0] : null;
    }

    function videoTrackSettings(track) {
        if (!track) return {};
        if (typeof track.getSettings === 'function') {
            try {
                return track.getSettings() || {};
            } catch (error) {
            }
        }
        return {};
    }

    function isCurrentTrack(track) {
        return !!(track && X.state.localStream && getVideoTrack(X.state.localStream) === track);
    }

    function trackLifecycleDetail(track) {
        var settings = videoTrackSettings(track);
        return 'readyState=' + track.readyState + ' enabled=' + track.enabled + ' muted=' + track.muted + ' callActive=' + X.state.callActive + ' generation=' + (track.generation || 0) + ' action=' + X.state.cameraLastAction + ' width=' + (settings.width || '?') + ' height=' + (settings.height || '?');
    }

    function logVideoTrack(prefix, track) {
        if (!track) {
            X.log.media(prefix + ' track=none cameraLabel=' + X.state.cameraDeviceLabel);
            return;
        }
        var settings = videoTrackSettings(track);
        X.log.media(prefix + ' readyState=' + track.readyState + ' enabled=' + track.enabled + ' muted=' + track.muted + ' callActive=' + X.state.callActive + ' generation=' + (track.generation || 0) + ' width=' + (settings.width || '?') + ' height=' + (settings.height || '?') + ' fps=' + (settings.frameRate || '?') + ' cameraLabel=' + X.state.cameraDeviceLabel);
    }

    function attachLocalPreview(stream) {
        var localVideo = X.dom.localVideo;
        if (!localVideo) return;
        if (localVideo.srcObject !== stream) {
            localVideo.srcObject = stream;
        }
        try {
            var promise = localVideo.play();
            if (promise && typeof promise.catch === 'function') {
                promise.catch(function (error) {
                    X.log.warn('Media', 'LOCAL_VIDEO_PLAY_FAIL ' + error);
                });
            }
        } catch (error) {
            X.log.warn('Media', 'LOCAL_VIDEO_PLAY_FAIL ' + error);
        }
    }

    function resolveProfile(profile) {
        if (!profile) return null;
        if (typeof profile === 'string') return cameraProfiles[profile] || null;
        if (profile && profile.width && profile.height) return profile;
        return null;
    }

    function profileSupportedByCaps(profile, caps) {
        if (!caps) return true;
        var w = caps.width || {};
        var h = caps.height || {};
        if (w.max === undefined || h.max === undefined) return true;
        if (w.max < profile.width || h.max < profile.height) return false;
        return true;
    }

    function logCapabilities(track) {
        if (!track || typeof track.getCapabilities !== 'function') {
            X.log.media('CAMERA_CAPABILITIES_UNAVAILABLE cameraLabel=' + X.state.cameraDeviceLabel);
            return;
        }
        try {
            var caps = track.getCapabilities() || {};
            var w = caps.width || {};
            var h = caps.height || {};
            var f = caps.frameRate || {};
            X.log.media('CAMERA_CAPABILITIES width.min=' + (w.min === undefined ? '?' : w.min) + ' width.max=' + (w.max === undefined ? '?' : w.max) + ' height.min=' + (h.min === undefined ? '?' : h.min) + ' height.max=' + (h.max === undefined ? '?' : h.max) + ' frameRate.min=' + (f.min === undefined ? '?' : f.min) + ' frameRate.max=' + (f.max === undefined ? '?' : f.max) + ' cameraLabel=' + X.state.cameraDeviceLabel);
        } catch (error) {
            X.log.media('CAMERA_CAPABILITIES_UNAVAILABLE error=' + error);
        }
    }

    function refreshCapabilities(track) {
        if (!track || typeof track.getCapabilities !== 'function') {
            X.state.cameraCapabilities = null;
            return;
        }
        try {
            X.state.cameraCapabilities = track.getCapabilities() || null;
        } catch (error) {
            X.state.cameraCapabilities = null;
        }
    }

    function constraintValue(value) {
        if (value === undefined || value === null) return '?';
        if (typeof value !== 'object') return X.text(value);
        if (value.exact !== undefined) return 'exact=' + value.exact;
        if (value.ideal !== undefined) return 'ideal=' + value.ideal;
        if (value.min !== undefined || value.max !== undefined) return 'min=' + (value.min === undefined ? '?' : value.min) + ' max=' + (value.max === undefined ? '?' : value.max);
        return '?';
    }

    function logConstraints(track) {
        if (!track || typeof track.getConstraints !== 'function') {
            X.log.media('CAMERA_CONSTRAINTS_UNAVAILABLE');
            return;
        }
        try {
            var raw = track.getConstraints() || {};
            var video = raw.video || raw;
            var width = constraintValue(video.width);
            var height = constraintValue(video.height);
            var frameRate = constraintValue(video.frameRate);
            X.log.media('CAMERA_CONSTRAINTS width=' + width + ' height=' + height + ' frameRate=' + frameRate + ' cameraLabel=' + X.state.cameraDeviceLabel);
        } catch (error) {
            X.log.media('CAMERA_CONSTRAINTS_UNAVAILABLE error=' + error);
        }
    }

    function refreshDeviceList() {
        if (!navigator.mediaDevices || typeof navigator.mediaDevices.enumerateDevices !== 'function') {
            X.state.cameraDeviceList = [];
            X.log.media('VIDEO_DEVICE_COUNT count=0');
            return Promise.resolve([]);
        }
        return navigator.mediaDevices.enumerateDevices().then(function (devices) {
            var list = [];
            var index = 0;
            devices.forEach(function (device) {
                if (device.kind !== 'videoinput') return;
                list.push({
                    index: index,
                    deviceId: device.deviceId || '',
                    label: device.label || ('camera-' + index)
                });
                index++;
            });
            X.state.cameraDeviceList = list;
            list.forEach(function (device) {
                X.log.media('VIDEO_DEVICE index=' + device.index + ' label=' + device.label + ' deviceId=<set> groupId=<set>');
            });
            X.log.media('VIDEO_DEVICE_COUNT count=' + list.length);
            return list;
        }).catch(function (error) {
            X.state.cameraDeviceList = [];
            X.log.media('VIDEO_DEVICE_COUNT count=0 error=' + error);
            return [];
        });
    }

    function deviceLabel(deviceId) {
        if (!deviceId) return '';
        var list = X.state.cameraDeviceList || [];
        for (var i = 0; i < list.length; i++) {
            if (list[i].deviceId === deviceId) return list[i].label;
        }
        return '';
    }

    function updateCameraLabel(deviceId) {
        var label = deviceLabel(deviceId);
        if (label) X.state.cameraDeviceLabel = label;
    }

    function detectDeviceClass() {
        var combined = (X.state.cameraDeviceLabel || '').toLowerCase();
        if (combined.indexOf('lrcp') >= 0 || combined.indexOf('0c45') >= 0) return 'lrcp';
        return 'generic';
    }

    function applyDeviceClass() {
        var cls = detectDeviceClass();
        X.state.cameraDeviceClass = cls;
        if (cls === 'lrcp') {
            X.state.cameraSafeProfiles = ['qvga', 'vga'];
            X.state.cameraExperimentalProfiles = ['hd', 'fhd'];
        } else {
            X.state.cameraSafeProfiles = ['qvga', 'vga', 'hd'];
            X.state.cameraExperimentalProfiles = ['fhd'];
        }
        X.log.media('CAMERA_PROFILE_CLASS class=' + cls + ' safe=' + X.state.cameraSafeProfiles.join(',') + ' experimental=' + X.state.cameraExperimentalProfiles.join(',') + ' cameraLabel=' + X.state.cameraDeviceLabel);
    }

    function clearMutedDebounce() {
        if (X.state.cameraMutedDebounceTimer) {
            clearTimeout(X.state.cameraMutedDebounceTimer);
            X.state.cameraMutedDebounceTimer = null;
        }
    }

    function markLocalCameraUnavailable(reason) {
        if (X.state.localCameraDisabled || X.state.cameraLastAction === 'user-disable') {
            X.log.media('LOCAL_CAMERA_DISABLED_IGNORE_UNAVAILABLE reason=' + reason + ' cameraLabel=' + X.state.cameraDeviceLabel);
            return;
        }
        if (X.state.localCameraUnavailable) {
            X.log.media('LOCAL_CAMERA_ALREADY_UNAVAILABLE reason=' + reason);
            return;
        }
        X.state.cameraLastAction = reason || 'camera-disconnect';
        X.state.cameraUnavailableReason = reason || 'unknown';
        if (X.ui.setLocalCameraUnavailable) X.ui.setLocalCameraUnavailable(true, reason || 'unknown');
        X.log.warn('Media', 'LOCAL_CAMERA_UNAVAILABLE reason=' + (reason || 'unknown') + ' cameraLabel=' + X.state.cameraDeviceLabel);
    }

    function attachTrackLifecycle(track) {
        if (!track) return;
        track.onended = function () {
            clearMutedDebounce();
            if (isCurrentTrack(track)) {
                if (!track.enabled || X.state.localCameraDisabled) {
                    X.log.media('CAMERA_DISABLED_TRACK_ENDED ' + trackLifecycleDetail(track));
                    return;
                }
                X.log.warn('Media', 'CURRENT_CAMERA_TRACK_ENDED ' + trackLifecycleDetail(track));
                markLocalCameraUnavailable('track-ended');
            } else {
                X.log.warn('Media', 'STALE_CAMERA_TRACK_ENDED ' + trackLifecycleDetail(track));
            }
        };
        track.onmute = function () {
            if (isCurrentTrack(track)) {
                if (!track.enabled || X.state.localCameraDisabled) {
                    X.log.media('CAMERA_DISABLED_TRACK_MUTED ' + trackLifecycleDetail(track));
                    return;
                }
                X.log.warn('Media', 'CURRENT_CAMERA_TRACK_MUTED ' + trackLifecycleDetail(track));
                clearMutedDebounce();
                X.state.cameraMutedDebounceTimer = setTimeout(function () {
                    X.state.cameraMutedDebounceTimer = null;
                    if (isCurrentTrack(track) && track.readyState === 'live' && track.muted) {
                        markLocalCameraUnavailable('track-muted');
                    }
                }, 1500);
            } else {
                X.log.warn('Media', 'STALE_CAMERA_TRACK_MUTED ' + trackLifecycleDetail(track));
            }
        };
        track.onunmute = function () {
            X.log.media('CAMERA_TRACK_UNMUTED ' + trackLifecycleDetail(track));
            clearMutedDebounce();
            if (isCurrentTrack(track) && track.readyState === 'live' && !track.muted && X.state.localCameraUnavailable) {
                X.log.media('CAMERA_TRACK_RECOVERED ' + trackLifecycleDetail(track));
                if (X.ui.setLocalCameraUnavailable) X.ui.setLocalCameraUnavailable(false);
            }
        };
    }

    function buildConstraints(profile) {
        return {
            video: {
                width: { exact: profile.width },
                height: { exact: profile.height },
                frameRate: { ideal: 30, max: 30 }
            },
            audio: false
        };
    }

    function stopWatchdog() {
        if (X.state.cameraWatchdogTimer) {
            clearInterval(X.state.cameraWatchdogTimer);
            X.state.cameraWatchdogTimer = null;
        }
        X.state.cameraWatchdogCount = 0;
        X.state.cameraWatchdogStallCapture = false;
        X.state.cameraWatchdogStallSend = false;
    }

    function startWatchdog(track, token) {
        stopWatchdog();
        if (!track) return;
        X.state.cameraWatchdogLastTime = X.dom.localVideo ? X.dom.localVideo.currentTime : 0;
        X.state.cameraWatchdogLastFrames = -1;
        X.state.cameraWatchdogTimer = setInterval(function () {
            X.state.cameraWatchdogCount++;
            if (token !== X.state.cameraSwitchToken) {
                stopWatchdog();
                return;
            }
            var video = X.dom.localVideo;
            var time = video ? video.currentTime : 0;
            var width = video ? video.videoWidth : 0;
            var height = video ? video.videoHeight : 0;
            var advance = time > X.state.cameraWatchdogLastTime;
            X.state.cameraWatchdogLastTime = time;
            if (!X.state.cameraWatchdogStallCapture && X.state.cameraWatchdogCount >= 3 && !advance) {
                X.state.cameraWatchdogStallCapture = true;
                X.log.warn('Media', 'CAMERA_CAPTURE_STALL profile=' + X.state.cameraActiveProfile + ' readyState=' + track.readyState + ' muted=' + track.muted + ' videoTime=' + time + ' width=' + width + ' height=' + height + ' cameraLabel=' + X.state.cameraDeviceLabel);
            }
            if (X.state.callActive && X.state.peerConnection && !X.state.cameraWatchdogStallSend) {
                X.peer.getVideoOutFrames(function (frames) {
                    if (token !== X.state.cameraSwitchToken) return;
                    if (X.state.cameraWatchdogLastFrames < 0) {
                        X.state.cameraWatchdogLastFrames = frames;
                        return;
                    }
                    if (frames <= X.state.cameraWatchdogLastFrames && X.state.cameraWatchdogCount >= 4) {
                        X.state.cameraWatchdogStallSend = true;
                        X.log.warn('Media', 'CAMERA_SEND_STALL profile=' + X.state.cameraActiveProfile + ' framesEncoded=' + frames + ' cameraLabel=' + X.state.cameraDeviceLabel);
                    } else {
                        X.state.cameraWatchdogLastFrames = frames;
                    }
                });
            }
            if (X.state.cameraWatchdogCount >= 6) stopWatchdog();
        }, 500);
    }

    function reacquireCamera(reason) {
        if (X.state.cameraReacquireInFlight) {
            X.log.media('CAMERA_REACQUIRE_SKIP reason=' + reason + ' inFlight=true');
            return;
        }
        if (X.state.localCameraDisabled) {
            X.log.media('CAMERA_REACQUIRE_SKIP reason=' + reason + ' userDisabled=true');
            return;
        }
        var now = Date.now();
        if (now < X.state.cameraReacquireCooldownUntil) {
            X.log.media('CAMERA_REACQUIRE_SKIP reason=' + reason + ' cooldown=true');
            return;
        }
        X.state.cameraReacquireCooldownUntil = now + 5000;
        X.state.cameraReacquireInFlight = true;
        X.log.media('CAMERA_REACQUIRE reason=' + reason + ' cameraLabel=' + X.state.cameraDeviceLabel);
        X.media.startPreview().then(function (stream) {
            X.state.cameraReacquireInFlight = false;
            if (stream) {
                X.log.media('CAMERA_REACQUIRE_OK cameraLabel=' + X.state.cameraDeviceLabel);
            } else {
                X.log.warn('Media', 'CAMERA_REACQUIRE_FAIL reason=' + reason);
            }
        }).catch(function (error) {
            X.state.cameraReacquireInFlight = false;
            X.log.warn('Media', 'CAMERA_REACQUIRE_FAIL reason=' + reason + ' error=' + error);
        });
    }

    function initDeviceChange() {
        if (!navigator.mediaDevices) return;
        navigator.mediaDevices.ondevicechange = function () {
            X.log.media('CAMERA_DEVICE_CHANGE');
            refreshDeviceList().then(function (list) {
                var track = getVideoTrack(X.state.localStream);
                var trackUsable = !!(track && track.readyState === 'live' && !track.muted);
                if (!trackUsable) {
                    if (list.length > 0) {
                        X.log.media('CAMERA_DEVICE_ADDED count=' + list.length);
                        reacquireCamera('device-added');
                    }
                    return;
                }
                var settings = videoTrackSettings(track);
                var deviceId = settings.deviceId || '';
                if (!deviceId) return;
                var found = false;
                for (var i = 0; i < list.length; i++) {
                    if (list[i].deviceId === deviceId) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    X.log.warn('Media', 'CAMERA_DEVICE_REMOVED cameraLabel=' + X.state.cameraDeviceLabel);
                    markLocalCameraUnavailable('device-removed');
                } else if (track.muted) {
                    markLocalCameraUnavailable('track-muted');
                } else if (X.state.localCameraUnavailable) {
                    X.log.media('CAMERA_DEVICE_RECONNECTED cameraLabel=' + X.state.cameraDeviceLabel);
                    if (X.ui.setLocalCameraUnavailable) X.ui.setLocalCameraUnavailable(false);
                }
            });
        };
    }

    function staleCleanup(newStream, profileName) {
        X.log.warn('Media', 'CAMERA_SWITCH_STALE_RESULT profile=' + profileName + ' cameraLabel=' + X.state.cameraDeviceLabel);
        if (newStream) stopTracks(newStream);
        X.state.cameraSwitchPending = false;
        if (X.ui.setCameraProfilePending) X.ui.setCameraProfilePending(profileName, false);
    }

    async function acquireInitial(profileName) {
        var resolved = resolveProfile(profileName);
        if (!resolved) {
            X.log.warn('Media', 'CAMERA_PROFILE_INVALID profile=' + X.text(profileName));
            return null;
        }
        var token = ++X.state.cameraSwitchToken;
        var generation = ++X.state.cameraGeneration;
        X.state.cameraLastAction = 'none';
        X.state.currentCameraProfile = resolved.name;
        if (!navigator.mediaDevices || typeof navigator.mediaDevices.getUserMedia !== 'function') {
            X.log.warn('Media', 'GET_USER_MEDIA_UNAVAILABLE');
            return null;
        }
        X.log.media('CAMERA_REQUEST profile=' + resolved.name + ' width=' + resolved.width + ' height=' + resolved.height + ' frameRateIdeal=30 frameRateMax=30 mode=exact-resolution cameraLabel=' + X.state.cameraDeviceLabel);
        try {
            var stream = await navigator.mediaDevices.getUserMedia(buildConstraints(resolved));
            if (token !== X.state.cameraSwitchToken) {
                staleCleanup(stream, resolved.name);
                return null;
            }
            var track = getVideoTrack(stream);
            if (!track) {
                X.log.warn('Media', 'CAMERA_NO_VIDEO_TRACK profile=' + resolved.name);
                stopTracks(stream);
                return null;
            }
            track.generation = generation;
            var settings = videoTrackSettings(track);
            updateCameraLabel(settings.deviceId);
            applyDeviceClass();
            refreshCapabilities(track);
            var previousStream = X.state.localStream;
            X.state.localStream = stream;
            X.state.cameraActiveProfile = resolved.name;
            attachLocalPreview(stream);
            X.state.localCameraDisabled = false;
            if (X.ui.setLocalCameraDisabled) X.ui.setLocalCameraDisabled(false);
            clearMutedDebounce();
            X.state.cameraUnavailableReason = '';
            if (previousStream && previousStream !== stream) stopTracks(previousStream);
            if (X.ui.setLocalCameraUnavailable) {
                X.ui.setLocalCameraUnavailable(false);
            } else if (X.ui.buildCameraProfileBar) {
                X.ui.buildCameraProfileBar();
            }
            if (X.state.callActive && X.state.peerConnection) {
                try {
                    await X.peer.replaceVideoTrack(track);
                } catch (error) {
                    X.log.warn('Media', 'CAMERA_REACQUIRE_REPLACE_FAIL ' + error);
                }
            }
            X.log.media('CAMERA_GRANTED profile=' + resolved.name + ' width=' + resolved.width + ' height=' + resolved.height + ' cameraLabel=' + X.state.cameraDeviceLabel);
            X.log.media('CAMERA_SETTINGS width=' + (settings.width || '?') + ' height=' + (settings.height || '?') + ' frameRate=' + (settings.frameRate || '?') + ' deviceId=<set> groupId=<set> cameraLabel=' + X.state.cameraDeviceLabel);
            logCapabilities(track);
            logConstraints(track);
            logVideoTrack('CAMERA_TRACK', track);
            attachTrackLifecycle(track);
            X.bridge.reportPreviewReady({
                width: settings.width || 0,
                height: settings.height || 0,
                frameRate: settings.frameRate || 0
            });
            refreshDeviceList();
            initDeviceChange();
            if (X.state.cameraProbeEnabled) X.probe.startProbe();
            return stream;
        } catch (error) {
            X.log.warn('Media', 'CAMERA_REQUEST_FAILED profile=' + resolved.name + ' name=' + (error && error.name ? error.name : 'unknown') + ' message=' + (error && error.message ? error.message : X.text(error)) + ' constraint=' + (error && error.constraint ? error.constraint : 'none') + ' cameraLabel=' + X.state.cameraDeviceLabel);
            return null;
        }
    }

    async function applyConstraintsSwitch(resolved, token, from, to, label, track, stream, oldActive, generation) {
        var requested = resolved.width + 'x' + resolved.height;
        X.log.media('CAMERA_APPLY_CONSTRAINTS_BEGIN from=' + from + ' to=' + to + ' requested=' + requested + ' cameraLabel=' + label);
        var oldConstraints = null;
        if (typeof track.getConstraints === 'function') {
            try {
                oldConstraints = track.getConstraints() || null;
            } catch (error) {
                oldConstraints = null;
            }
        }
        try {
            await track.applyConstraints({
                width: { exact: resolved.width },
                height: { exact: resolved.height },
                frameRate: { ideal: 30, max: 30 }
            });
        } catch (error) {
            var reason = (error && error.name ? error.name : 'unknown');
            if (error && error.message) reason += ' ' + error.message;
            if (error && error.constraint) reason += ' constraint=' + error.constraint;
            X.log.warn('Media', 'CAMERA_APPLY_CONSTRAINTS_ROLLBACK from=' + from + ' to=' + to + ' reason=' + reason + ' cameraLabel=' + label);
            if (oldConstraints) {
                try {
                    await track.applyConstraints(oldConstraints);
                    X.log.media('CAMERA_APPLY_CONSTRAINTS_RESTORED from=' + to + ' to=' + from + ' cameraLabel=' + label);
                } catch (restoreError) {
                    X.log.warn('Media', 'CAMERA_APPLY_CONSTRAINTS_RESTORE_FAIL ' + restoreError);
                }
            }
            throw error;
        }
        if (token !== X.state.cameraSwitchToken) {
            X.log.warn('Media', 'CAMERA_SWITCH_STALE_RESULT profile=' + to + ' cameraLabel=' + label);
            return null;
        }
        var settings = videoTrackSettings(track);
        var actualW = settings.width || '?';
        var actualH = settings.height || '?';
        var actualFps = settings.frameRate || '?';
        X.log.media('CAMERA_APPLY_CONSTRAINTS_OK requested=' + requested + ' actual=' + actualW + 'x' + actualH + ' fps=' + actualFps + ' cameraLabel=' + label);
        X.log.media('CAMERA_SETTINGS width=' + actualW + ' height=' + actualH + ' frameRate=' + actualFps + ' deviceId=<set> groupId=<set> cameraLabel=' + label);
        logCapabilities(track);
        logConstraints(track);
        logVideoTrack('CAMERA_TRACK', track);
        X.state.localStream = stream;
        X.state.cameraActiveProfile = to;
        X.state.currentCameraProfile = to;
        X.state.cameraSwitchPending = false;
        X.state.cameraLastAction = 'none';
        if (X.ui.setCameraProfilePending) X.ui.setCameraProfilePending(to, false);
        if (X.ui.setLocalCameraUnavailable) {
            X.ui.setLocalCameraUnavailable(false);
        } else if (X.ui.buildCameraProfileBar) {
            X.ui.buildCameraProfileBar();
        }
        X.log.media('CAMERA_SWITCH_COMMIT profile=' + to + ' mode=applyConstraints cameraLabel=' + label);
        X.bridge.reportPreviewReady({
            width: settings.width || 0,
            height: settings.height || 0,
            frameRate: settings.frameRate || 0
        });
        attachLocalPreview(stream);
        refreshDeviceList();
        startWatchdog(track, token);
        return stream;
    }

    async function executeSwitch(profileName) {
        var resolved = resolveProfile(profileName);
        if (!resolved) {
            throw new Error('invalid profile ' + X.text(profileName));
        }
        var token = X.state.cameraSwitchToken;
        var from = X.state.cameraActiveProfile;
        var to = resolved.name;
        var label = X.state.cameraDeviceLabel;
        var oldStream = X.state.localStream;
        var oldTrack = getVideoTrack(oldStream);
        var oldActive = X.state.cameraActiveProfile;
        var newStream = null;
        X.state.cameraSwitchPending = true;
        if (X.ui.setCameraProfilePending) X.ui.setCameraProfilePending(to, true);
        X.log.media('CAMERA_SWITCH_BEGIN from=' + from + ' to=' + to + ' cameraLabel=' + label);
        X.state.cameraLastAction = 'profile-switch';
        var generation = ++X.state.cameraGeneration;
        try {
            if (oldTrack && oldTrack.readyState === 'live' && typeof oldTrack.applyConstraints === 'function') {
                return await applyConstraintsSwitch(resolved, token, from, to, label, oldTrack, oldStream, oldActive, generation);
            }
            if (!navigator.mediaDevices || typeof navigator.mediaDevices.getUserMedia !== 'function') {
                throw new Error('getUserMedia unavailable');
            }
            X.log.media('CAMERA_REQUEST profile=' + to + ' width=' + resolved.width + ' height=' + resolved.height + ' frameRateIdeal=30 frameRateMax=30 mode=exact-resolution cameraLabel=' + label);
            newStream = await navigator.mediaDevices.getUserMedia(buildConstraints(resolved));
            if (token !== X.state.cameraSwitchToken) {
                staleCleanup(newStream, to);
                return null;
            }
            var track = getVideoTrack(newStream);
            if (!track) {
                throw new Error('no video track');
            }
            track.generation = generation;
            var settings = videoTrackSettings(track);
            if (!settings.width || !settings.height) {
                throw new Error('invalid track settings');
            }
            updateCameraLabel(settings.deviceId);
            label = X.state.cameraDeviceLabel;
            applyDeviceClass();
            refreshCapabilities(track);
            X.log.media('CAMERA_SWITCH_NEW_TRACK_READY profile=' + to + ' width=' + settings.width + ' height=' + settings.height + ' fps=' + (settings.frameRate || '?') + ' cameraLabel=' + label);
            X.log.media('CAMERA_SETTINGS width=' + (settings.width || '?') + ' height=' + (settings.height || '?') + ' frameRate=' + (settings.frameRate || '?') + ' deviceId=<set> groupId=<set> cameraLabel=' + label);
            logCapabilities(track);
            logConstraints(track);
            if (X.state.callActive && X.state.peerConnection) {
                await X.peer.replaceVideoTrack(track);
            }
            if (token !== X.state.cameraSwitchToken) {
                staleCleanup(newStream, to);
                return null;
            }
            X.state.localStream = newStream;
            attachLocalPreview(newStream);
            X.state.cameraActiveProfile = to;
            X.state.currentCameraProfile = to;
            X.state.cameraSwitchPending = false;
            X.state.cameraLastAction = 'none';
            X.state.localCameraDisabled = false;
            if (X.ui.setLocalCameraDisabled) X.ui.setLocalCameraDisabled(false);
            if (X.ui.setCameraProfilePending) X.ui.setCameraProfilePending(to, false);
            if (X.ui.setLocalCameraUnavailable) {
                X.ui.setLocalCameraUnavailable(false);
            } else if (X.ui.buildCameraProfileBar) {
                X.ui.buildCameraProfileBar();
            }
            X.log.media('CAMERA_SWITCH_COMMIT profile=' + to + ' cameraLabel=' + label);
            logVideoTrack('CAMERA_TRACK', track);
            X.bridge.reportPreviewReady({
                width: settings.width || 0,
                height: settings.height || 0,
                frameRate: settings.frameRate || 0
            });
            attachTrackLifecycle(track);
            if (oldTrack && oldTrack !== track) {
                if (oldStream) stopTracks(oldStream);
                X.log.media('CAMERA_OLD_TRACK_STOPPED from=' + from + ' to=' + to);
            }
            refreshDeviceList();
            startWatchdog(track, token);
            return newStream;
        } catch (error) {
            X.state.cameraSwitchPending = false;
            if (X.ui.setCameraProfilePending) X.ui.setCameraProfilePending(to, false);
            if (newStream) {
                stopTracks(newStream);
                newStream = null;
            }
            if (oldStream) {
                X.state.localStream = oldStream;
                attachLocalPreview(oldStream);
            }
            X.state.cameraActiveProfile = oldActive;
            X.state.currentCameraProfile = oldActive;
            X.state.cameraLastAction = 'none';
            var reason = (error && error.name ? error.name : 'unknown');
            if (error && error.message) reason += ' ' + error.message;
            if (error && error.constraint) reason += ' constraint=' + error.constraint;
            X.log.warn('Media', 'CAMERA_SWITCH_ROLLBACK from=' + from + ' to=' + to + ' reason=' + reason + ' cameraLabel=' + label);
            throw error;
        }
    }

    function runSwitchQueue() {
        if (X.state.cameraSwitchInFlight) return X.state.cameraSwitchPromise || Promise.resolve(null);
        var pending = X.state.pendingProfileSwitch;
        if (!pending) return Promise.resolve(null);
        X.state.pendingProfileSwitch = null;
        X.state.cameraSwitchInFlight = true;
        X.state.cameraSwitchPromise = executeSwitch(pending).then(function (result) {
            X.state.cameraSwitchInFlight = false;
            X.state.cameraSwitchPromise = null;
            if (X.state.pendingProfileSwitch) return runSwitchQueue();
            return result;
        }).catch(function (error) {
            X.state.cameraSwitchInFlight = false;
            X.state.cameraSwitchPromise = null;
            if (X.state.pendingProfileSwitch) return runSwitchQueue();
            throw error;
        });
        return X.state.cameraSwitchPromise;
    }

    function setCameraProfile(profileName) {
        var resolved = resolveProfile(profileName);
        if (!resolved) {
            return Promise.reject(new Error('invalid profile ' + X.text(profileName)));
        }
        var safe = X.state.cameraSafeProfiles.indexOf(resolved.name) >= 0;
        var experimental = X.state.cameraExperimentalProfiles.indexOf(resolved.name) >= 0;
        if (!safe && !(experimental && X.state.cameraProbeEnabled)) {
            return Promise.reject(new Error('profile not available ' + resolved.name));
        }
        if (X.state.localCameraUnavailable) {
            return Promise.reject(new Error('camera unavailable'));
        }
        X.state.pendingProfileSwitch = resolved.name;
        X.log.media('CAMERA_SWITCH_REQUEST from=' + X.state.cameraActiveProfile + ' to=' + resolved.name + ' cameraLabel=' + X.state.cameraDeviceLabel);
        return runSwitchQueue();
    }

    async function startPreview() {
        var existingTrack = getVideoTrack(X.state.localStream);
        if (existingTrack && existingTrack.readyState === 'live' && !X.state.localCameraUnavailable) {
            attachLocalPreview(X.state.localStream);
            logVideoTrack('PREVIEW_REUSE', existingTrack);
            return X.state.localStream;
        }
        if (X.state.previewPromise) {
            X.log.media('PREVIEW_WAIT_EXISTING');
            return X.state.previewPromise;
        }
        X.state.previewPromise = acquireInitial(X.state.currentCameraProfile).then(function (stream) {
            X.state.previewPromise = null;
            return stream;
        });
        return X.state.previewPromise;
    }

    function getCurrentCameraProfile() {
        return X.state.cameraActiveProfile;
    }

    function getUIVisibleProfiles() {
        var caps = X.state.cameraCapabilities;
        var result = [];
        X.state.cameraSafeProfiles.forEach(function (name) {
            var profile = cameraProfiles[name];
            if (!profile) return;
            if (caps && !profileSupportedByCaps(profile, caps)) return;
            result.push(profile);
        });
        return result;
    }

    function getExperimentalProfiles() {
        var result = [];
        X.state.cameraExperimentalProfiles.forEach(function (name) {
            var profile = cameraProfiles[name];
            if (profile) result.push(profile);
        });
        return result;
    }

    function getProfileInfo(profileName) {
        var profile = resolveProfile(profileName);
        if (!profile) return null;
        return {
            name: profile.name,
            label: profile.label,
            uiLabel: profile.uiLabel,
            width: profile.width,
            height: profile.height,
            safe: X.state.cameraSafeProfiles.indexOf(profile.name) >= 0,
            experimental: X.state.cameraExperimentalProfiles.indexOf(profile.name) >= 0
        };
    }

    function getCameraLabel() {
        return X.state.cameraDeviceLabel;
    }

    function stopLocalStream(action) {
        X.state.cameraLastAction = action || 'stop-call';
        stopWatchdog();
        clearMutedDebounce();
        X.state.cameraSwitchToken++;
        X.state.cameraSwitchPending = false;
        X.state.pendingProfileSwitch = null;
        var track = getVideoTrack(X.state.localStream);
        if (X.state.localStream) {
            X.log.media('LOCAL_STREAM_STOPPED action=' + X.state.cameraLastAction + ' generation=' + (track ? track.generation || 0 : 0) + ' cameraLabel=' + X.state.cameraDeviceLabel);
            stopTracks(X.state.localStream);
            X.state.localStream = null;
        }
        X.state.cameraDeviceLabel = '';
        if (X.dom.localVideo) {
            X.dom.localVideo.srcObject = null;
        }
    }

    function setCameraEnabled(enabled) {
        var track = getVideoTrack(X.state.localStream);
        if (!track) {
            X.log.warn('Media', 'CAMERA_TOGGLE_NO_TRACK enabled=' + !!enabled);
            return;
        }
        X.state.cameraLastAction = enabled ? 'none' : 'user-disable';
        X.state.localCameraDisabled = !enabled;
        if (!enabled) clearMutedDebounce();
        track.enabled = !!enabled;
        if (X.ui.setLocalCameraDisabled) X.ui.setLocalCameraDisabled(!enabled);
        X.log.media('CAMERA_ENABLED=' + track.enabled + ' action=' + X.state.cameraLastAction + ' cameraLabel=' + X.state.cameraDeviceLabel);
    }

    function init() {
        applyDeviceClass();
        initDeviceChange();
        refreshDeviceList();
    }

    X.media = {
        cameraProfiles: cameraProfiles,
        startPreview: startPreview,
        setCameraProfile: setCameraProfile,
        getCurrentCameraProfile: getCurrentCameraProfile,
        getUIVisibleProfiles: getUIVisibleProfiles,
        getExperimentalProfiles: getExperimentalProfiles,
        getProfileInfo: getProfileInfo,
        getCameraLabel: getCameraLabel,
        stopLocalStream: stopLocalStream,
        setCameraEnabled: setCameraEnabled,
        getVideoTrack: getVideoTrack,
        videoTrackSettings: videoTrackSettings,
        logVideoTrack: logVideoTrack,
        attachLocalPreview: attachLocalPreview,
        refreshDeviceList: refreshDeviceList,
        init: init
    };

    X.log.media('loaded');
}(window.Xiaofu = window.Xiaofu || {}));

