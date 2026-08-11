(function (X) {
    'use strict';

    X.state = {
        bridge: null,
        iceTransportPolicy: 'relay',
        iceServers: [],
        localStream: null,
        localAudioStream: null,
        audioTransceiver: null,
        audioSender: null,
        micEnabled: true,
        audioDeviceList: [],
        previewPromise: null,
        peerConnection: null,
        videoTransceiver: null,
        videoSender: null,
        pendingCandidates: [],
        callActive: false,
        outgoingOfferStarted: false,
        callToken: 0,
        pcSeq: 0,
        currentPcId: 0,
        statsTimer: null,
        statsCount: 0,
        maxStatsCount: 40,
        cameraGeneration: 0,
        cameraSwitchToken: 0,
        cameraSwitchInFlight: false,
        cameraSwitchPromise: null,
        pendingProfileSwitch: null,
        cameraSwitchPending: false,
        localCameraUnavailable: false,
        localCameraDisabled: false,
        cameraMutedDebounceTimer: null,
        cameraUnavailableReason: '',
        cameraReacquireInFlight: false,
        cameraReacquireCooldownUntil: 0,
        cameraActiveProfile: 'vga',
        cameraDeviceList: [],
        cameraDeviceLabel: '',
        cameraDeviceClass: 'generic',
        cameraCapabilities: null,
        cameraSafeProfiles: ['qvga', 'vga', 'hd'],
        cameraLastAction: 'none',
        cameraWatchdogTimer: null,
        cameraWatchdogCount: 0,
        cameraWatchdogLastTime: 0,
        cameraWatchdogLastFrames: 0,
        cameraWatchdogStallCapture: false,
        cameraWatchdogStallSend: false,
        currentCameraProfile: 'vga',
        ui: {
            pipMoved: false,
            pipX: null,
            pipY: null
        },
        screen: {
            supported: false,
            active: false,
            stream: null,
            track: null
        },
        recorder: {
            supported: false,
            recording: false,
            startedAt: 0,
            mimeType: ''
        }
    };

    X.log.state('loaded');
}(window.Xiaofu = window.Xiaofu || {}));

