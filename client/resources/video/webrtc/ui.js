(function (X) {
    'use strict';

    var pipDrag = null;
    var pipDoubleClickTimer = 0;
    var pipMargin = 12;
    var pipTopDefault = 16;
    var pipRightDefault = 26;

    function pipElement() {
        return X.dom.localPip || X.dom.localVideo;
    }

    function pipStage() {
        return X.dom.callStage || document.getElementById('call-stage');
    }

    function controlBar() {
        return document.getElementById('call-controls');
    }

    function pipSize() {
        var video = pipElement();
        if (!video) return { width: 220, height: 300 };
        return {
            width: video.offsetWidth || 220,
            height: video.offsetHeight || 300
        };
    }

    function clampRect(x, y) {
        var stage = pipStage();
        var video = pipElement();
        if (!stage || !video) return { x: x, y: y };
        var videoRect = video.getBoundingClientRect();
        var bar = controlBar();
        var barHeight = 0;
        if (bar && bar.offsetHeight) barHeight = bar.offsetHeight + pipMargin;
        var maxX = stage.clientWidth - videoRect.width - pipMargin;
        var maxY = stage.clientHeight - videoRect.height - barHeight - pipMargin;
        if (maxX < pipMargin) maxX = pipMargin;
        if (maxY < pipMargin) maxY = pipMargin;
        return {
            x: Math.max(pipMargin, Math.min(x, maxX)),
            y: Math.max(pipMargin, Math.min(y, maxY))
        };
    }

    function applyPipPosition(x, y) {
        var video = pipElement();
        if (!video) return;
        var clamped = clampRect(x, y);
        video.style.right = 'auto';
        video.style.left = clamped.x + 'px';
        video.style.top = clamped.y + 'px';
        X.state.ui.pipX = clamped.x;
        X.state.ui.pipY = clamped.y;
        X.state.ui.pipMoved = true;
    }

    function resetLocalPipPosition() {
        var video = pipElement();
        if (!video) return;
        video.style.left = 'auto';
        video.style.right = pipRightDefault + 'px';
        video.style.top = pipTopDefault + 'px';
        X.state.ui.pipMoved = false;
        X.state.ui.pipX = null;
        X.state.ui.pipY = null;
        X.log.ui('PIP_RESET_DEFAULT');
    }

    function reClamp() {
        var video = pipElement();
        if (!video) return;
        if (!X.state.ui.pipMoved) return;
        var beforeX = video.offsetLeft;
        var beforeY = video.offsetTop;
        var clamped = clampRect(beforeX, beforeY);
        if (clamped.x !== beforeX || clamped.y !== beforeY) {
            video.style.right = 'auto';
            video.style.left = clamped.x + 'px';
            video.style.top = clamped.y + 'px';
            X.state.ui.pipX = clamped.x;
            X.state.ui.pipY = clamped.y;
            X.log.ui('PIP_RECLAMP x=' + clamped.x + ' y=' + clamped.y);
        }
    }

    function usePointerEvents() {
        return typeof PointerEvent !== 'undefined';
    }

    function onPointerDown(event) {
        var video = pipElement();
        if (!video) return;
        if (event.type === 'mousedown' && event.button !== 0) return;
        pipDrag = {
            startX: event.clientX,
            startY: event.clientY,
            originLeft: video.offsetLeft,
            originTop: video.offsetTop,
            moved: false
        };
        if (event.type === 'pointerdown' && typeof video.setPointerCapture === 'function') {
            try {
                video.setPointerCapture(event.pointerId);
            } catch (error) {
            }
        }
        if (event.preventDefault) event.preventDefault();
        X.log.ui('PIP_DRAG_START x=' + event.clientX + ' y=' + event.clientY);
    }

    function onPointerMove(event) {
        if (!pipDrag) return;
        var video = pipElement();
        if (!video) return;
        var dx = event.clientX - pipDrag.startX;
        var dy = event.clientY - pipDrag.startY;
        if (Math.abs(dx) < 3 && Math.abs(dy) < 3) return;
        pipDrag.moved = true;
        var clamped = clampRect(pipDrag.originLeft + dx, pipDrag.originTop + dy);
        video.style.right = 'auto';
        video.style.left = clamped.x + 'px';
        video.style.top = clamped.y + 'px';
        X.state.ui.pipX = clamped.x;
        X.state.ui.pipY = clamped.y;
        X.state.ui.pipMoved = true;
        if (event.preventDefault) event.preventDefault();
    }

    function onPointerEnd(event) {
        if (!pipDrag) return;
        var video = pipElement();
        var moved = pipDrag.moved;
        pipDrag = null;
        if (video && typeof video.releasePointerCapture === 'function' && event.pointerId !== undefined) {
            try {
                video.releasePointerCapture(event.pointerId);
            } catch (error) {
            }
        }
        if (!moved) {
            if (pipDoubleClickTimer) {
                clearTimeout(pipDoubleClickTimer);
                pipDoubleClickTimer = 0;
                resetLocalPipPosition();
                return;
            }
            pipDoubleClickTimer = setTimeout(function () {
                pipDoubleClickTimer = 0;
            }, 260);
            return;
        }
        if (X.state.ui.pipX !== null && X.state.ui.pipY !== null) {
            X.log.ui('PIP_DRAG_END x=' + X.state.ui.pipX + ' y=' + X.state.ui.pipY);
        } else {
            X.log.ui('PIP_DRAG_END x=' + event.clientX + ' y=' + event.clientY);
        }
    }

    function bindDrag() {
        var video = pipElement();
        if (!video) return false;
        var usePointer = usePointerEvents();
        if (usePointer) {
            video.addEventListener('pointerdown', onPointerDown);
            video.addEventListener('pointermove', onPointerMove);
            video.addEventListener('pointerup', onPointerEnd);
            video.addEventListener('pointercancel', onPointerEnd);
        } else {
            video.addEventListener('mousedown', onPointerDown);
            document.addEventListener('mousemove', onPointerMove);
            document.addEventListener('mouseup', onPointerEnd);
        }
        X.log.ui('PIP_DRAG_READY mode=' + (usePointer ? 'pointer' : 'mouse'));
        return true;
    }

    function findButton(selector) {
        return document.querySelector(selector);
    }

    function setScreenShareState(state) {
        X.state.screen.active = !!state;
        var button = findButton('#share-button');
        if (button) {
            button.textContent = state ? '停止共享屏幕' : '共享屏幕';
            button.setAttribute('data-active', state ? 'true' : 'false');
        }
        X.log.ui('SCREEN_SHARE_STATE active=' + !!state);
    }

    function setRecordingState(state) {
        X.state.recorder.recording = !!state;
        var button = findButton('#record-button');
        if (button) {
            button.textContent = state ? '停止录制通话' : '录制通话';
            button.setAttribute('data-active', state ? 'true' : 'false');
        }
        X.log.ui('RECORDING_STATE recording=' + !!state);
    }

    function setFullscreenState(active) {
        active = !!active;
        var button = findButton('#fullscreen-button');
        if (button) {
            button.textContent = active ? '退出全屏' : '全屏';
            button.setAttribute('data-active', active ? 'true' : 'false');
        }
        X.log.ui(active ? 'FULLSCREEN_ENTER' : 'FULLSCREEN_EXIT');
    }

    function setSubtitleState(active) {
        var button = findButton('#subtitle-button');
        if (button) {
            button.setAttribute('data-active', active ? 'true' : 'false');
            button.textContent = active ? '关闭字幕' : '实时字幕';
        }
        X.log.ui('SUBTITLE_STATE active=' + !!active);
    }


    function morePopover() {
        return document.getElementById('more-popover');
    }

    function positionMorePopover() {
        var button = document.getElementById('more-button');
        var popover = morePopover();
        if (!button || !popover) return;
        var rect = button.getBoundingClientRect();
        var width = popover.offsetWidth || 128;
        var baseLeft = rect.left + rect.width / 2 - width / 2;
        var clampedLeft = Math.max(8, Math.min(baseLeft, window.innerWidth - width - 8));
        var shift = clampedLeft - baseLeft;
        popover.style.transform = 'translateX(calc(-50% + ' + shift + 'px))';
    }

    function repositionMoreMenu() {
        if (isMoreMenuOpen()) positionMorePopover();
    }

    function isMoreMenuOpen() {
        var popover = morePopover();
        return !!(popover && popover.className.indexOf('hidden') === -1);
    }

    function openMoreMenu() {
        var popover = morePopover();
        if (!popover) return;
        if (!isMoreMenuOpen()) {
            popover.className = popover.className.replace(/(^|\s)hidden(\s|$)/, ' ').trim();
        }
        positionMorePopover();
        X.log.ui('MORE_MENU_OPEN');
    }

    function closeMoreMenu() {
        var popover = morePopover();
        if (!popover) return;
        if (isMoreMenuOpen()) {
            popover.className = (popover.className + ' hidden').trim();
            X.log.ui('MORE_MENU_CLOSE');
        }
    }

    function toggleMoreMenu() {
        if (isMoreMenuOpen()) {
            closeMoreMenu();
        } else {
            openMoreMenu();
        }
    }

    function onDocumentClick(event) {
        var popover = morePopover();
        var moreButton = document.getElementById('more-button');
        if (!popover || !moreButton) return;
        if (popover.contains(event.target) || moreButton.contains(event.target)) return;
        closeMoreMenu();
    }

    function hasClass(el, cls) {
        if (!el || !el.className) return false;
        return (' ' + el.className + ' ').indexOf(' ' + cls + ' ') >= 0;
    }

    function addClass(el, cls) {
        if (!el || hasClass(el, cls)) return;
        el.className = (el.className + ' ' + cls).trim();
    }

    function removeClass(el, cls) {
        if (!el || !hasClass(el, cls)) return;
        el.className = (' ' + el.className + ' ').replace(' ' + cls + ' ', ' ').trim();
    }

    function localCameraOverlay() {
        return document.getElementById('local-camera-overlay');
    }

    function camButton() {
        return document.getElementById('cam-button');
    }

    function setLocalCameraUnavailable(unavailable, reason) {
        unavailable = !!unavailable;
        X.state.localCameraUnavailable = unavailable;
        X.state.cameraUnavailableReason = unavailable ? (reason || 'unknown') : '';
        var overlay = localCameraOverlay();
        if (overlay) {
            if (unavailable) {
                var muted = reason === 'track-muted' || reason === 'device-muted';
                overlay.textContent = muted ? '摄像头暂时无画面' : '暂无摄像头';
                removeClass(overlay, 'hidden');
            } else if (!X.state.localCameraDisabled) {
                addClass(overlay, 'hidden');
            } else {
                overlay.textContent = '摄像头已关闭';
            }
        }
        var button = camButton();
        if (button) {
            button.setAttribute('data-active', (unavailable || X.state.localCameraDisabled) ? 'false' : 'true');
        }
        buildCameraProfileBar();
        if (unavailable) {
            X.log.ui('LOCAL_CAMERA_UNAVAILABLE');
        } else {
            X.log.ui('LOCAL_CAMERA_RECOVERED');
        }
    }

    function setLocalCameraDisabled(disabled) {
        disabled = !!disabled;
        X.state.localCameraDisabled = disabled;
        var overlay = localCameraOverlay();
        if (overlay) {
            if (disabled) {
                overlay.textContent = '摄像头已关闭';
                removeClass(overlay, 'hidden');
            } else if (!X.state.localCameraUnavailable) {
                addClass(overlay, 'hidden');
            }
        }
        var button = camButton();
        if (button) {
            button.setAttribute('data-active', (disabled || X.state.localCameraUnavailable) ? 'false' : 'true');
        }
        if (disabled) {
            X.log.ui('LOCAL_CAMERA_DISABLED');
        } else {
            X.log.ui('LOCAL_CAMERA_ENABLED');
        }
    }

    var toastTimer = 0;

    function showToast(message) {
        var el = document.getElementById('ui-toast');
        if (!el) {
            el = document.createElement('div');
            el.id = 'ui-toast';
            el.style.cssText = 'position:fixed;left:50%;bottom:84px;transform:translateX(-50%);z-index:4000;background:rgba(0,0,0,.78);color:#FFFFFF;font:12px "Microsoft YaHei UI",sans-serif;padding:8px 14px;border-radius:6px;pointer-events:none;';
            document.body.appendChild(el);
        }
        el.textContent = message;
        el.style.display = 'block';
        if (toastTimer) clearTimeout(toastTimer);
        toastTimer = setTimeout(function () {
            el.style.display = 'none';
            toastTimer = 0;
        }, 2000);
        X.log.ui('TOAST ' + message);
    }

    function setScreenShareUnsupported(flag) {
        var button = findButton('#share-button');
        if (button) {
            if (flag) {
                button.setAttribute('data-unsupported', 'true');
                button.title = '当前版本暂不支持屏幕共享';
                button.setAttribute('aria-label', '共享屏幕（当前版本暂不支持）');
            } else {
                button.removeAttribute('data-unsupported');
                button.title = '共享屏幕';
                button.setAttribute('aria-label', '共享屏幕');
            }
        }
        X.log.ui('SCREEN_SHARE_UNSUPPORTED flag=' + !!flag);
    }

    function init() {
        var stage = pipStage();
        var video = pipElement();
        if (!stage || !video) {
            X.log.warn('UI', 'PIP_ELEMENT_MISSING');
            return;
        }
        bindDrag();
        var moreButton = document.getElementById('more-button');
        if (moreButton) {
            moreButton.addEventListener('click', toggleMoreMenu);
        }
        document.addEventListener('click', onDocumentClick);
        window.addEventListener('resize', function () {
            reClamp();
            repositionMoreMenu();
        });
        X.log.ui('INIT');
    }

    function cameraProfileBar() {
        return document.getElementById('camera-profile-bar');
    }

    function buildCameraProfileBar() {
        var bar = cameraProfileBar();
        if (!bar) return;
        bar.innerHTML = '';
        if (X.state.localCameraUnavailable) {
            bar.style.display = 'none';
            X.log.ui('CAMERA_PROFILE_BAR_HIDDEN reason=no-camera');
            return;
        }
        bar.style.display = 'flex';
        var profiles = X.media.getUIVisibleProfiles ? X.media.getUIVisibleProfiles() : [];
        for (var i = 0; i < profiles.length; i++) {
            var profile = profiles[i];
            if (!profile) continue;
            var button = document.createElement('button');
            button.type = 'button';
            button.setAttribute('data-profile', profile.name);
            var label = profile.uiLabel + ' ' + profile.label;
            button.setAttribute('data-label', label);
            button.textContent = label;
            button.setAttribute('data-active', 'false');
            (function (name) {
                button.addEventListener('click', function () {
                    X.log.ui('CAMERA_PROFILE_CLICK profile=' + name);
                    X.media.setCameraProfile(name).catch(function (error) {
                        X.log.warn('UI', 'CAMERA_PROFILE_SWITCH_FAIL ' + error);
                    });
                });
            })(profile.name);
            bar.appendChild(button);
        }
        refreshCameraProfileActive();
        X.log.ui('CAMERA_PROFILE_BAR_BUILT count=' + profiles.length);
    }

    function refreshCameraProfileActive() {
        var bar = cameraProfileBar();
        if (!bar) return;
        var current = X.media.getCurrentCameraProfile ? X.media.getCurrentCameraProfile() : '';
        var track = X.media.getVideoTrack ? X.media.getVideoTrack(X.state.localStream) : null;
        var settings = track ? X.media.videoTrackSettings(track) : {};
        var fps = settings.frameRate || 0;
        var buttons = bar.querySelectorAll('button');
        for (var i = 0; i < buttons.length; i++) {
            var button = buttons[i];
            var name = button.getAttribute('data-profile');
            var active = name === current;
            button.setAttribute('data-active', active ? 'true' : 'false');
            var base = button.getAttribute('data-label') || button.textContent;
            if (active && fps > 0) {
                button.textContent = base + ' @' + Math.round(fps) + 'fps';
            } else {
                button.textContent = base;
            }
        }
    }

    function setCameraProfilePending(profileName, pending) {
        var bar = cameraProfileBar();
        if (!bar) return;
        var buttons = bar.querySelectorAll('button');
        for (var i = 0; i < buttons.length; i++) {
            var button = buttons[i];
            if (button.getAttribute('data-profile') !== profileName) continue;
            var label = button.getAttribute('data-label') || button.textContent;
            button.disabled = !!pending;
            button.setAttribute('data-pending', pending ? 'true' : 'false');
            button.textContent = pending ? (label + ' ...') : label;
        }
        if (!pending) refreshCameraProfileActive();
        X.log.ui('CAMERA_PROFILE_PENDING profile=' + profileName + ' pending=' + (pending ? 'true' : 'false'));
    }

    var callTimerInterval = 0;
    var callTimerSeconds = 0;

    function updateCallTimer() {
        callTimerSeconds++;
        var el = document.getElementById('call-timer');
        if (!el) return;
        var m = Math.floor(callTimerSeconds / 60);
        var s = callTimerSeconds % 60;
        el.textContent = (m < 10 ? '0' + m : '' + m) + ':' + (s < 10 ? '0' + s : '' + s);
    }

    function startCallTimer() {
        if (callTimerInterval) return;
        callTimerSeconds = 0;
        var el = document.getElementById('call-timer');
        if (el) {
            el.textContent = '00:00';
            el.style.display = 'block';
        }
        callTimerInterval = setInterval(updateCallTimer, 1000);
        X.log.ui('CALL_TIMER_START');
    }

    function stopCallTimer() {
        if (callTimerInterval) {
            clearInterval(callTimerInterval);
            callTimerInterval = 0;
        }
        callTimerSeconds = 0;
        var el = document.getElementById('call-timer');
        if (el) {
            el.textContent = '00:00';
            el.style.display = 'none';
        }
        X.log.ui('CALL_TIMER_STOP');
    }
    X.ui = {
        init: init,
        initLocalPipDrag: bindDrag,
        resetLocalPipPosition: resetLocalPipPosition,
        setScreenShareState: setScreenShareState,
        setRecordingState: setRecordingState,
        closeMoreMenu: closeMoreMenu,
        reClamp: reClamp,
        buildCameraProfileBar: buildCameraProfileBar,
        refreshCameraProfileActive: refreshCameraProfileActive,
        setLocalCameraUnavailable: setLocalCameraUnavailable,
        setLocalCameraDisabled: setLocalCameraDisabled,
        setCameraProfilePending: setCameraProfilePending,
        setFullscreenState: setFullscreenState,
        showToast: showToast,
        setScreenShareUnsupported: setScreenShareUnsupported,
        repositionMoreMenu: repositionMoreMenu,
        startCallTimer: startCallTimer,
        stopCallTimer: stopCallTimer,
        setSubtitleState: setSubtitleState
    };

    X.log.ui('loaded');
}(window.Xiaofu = window.Xiaofu || {}));


