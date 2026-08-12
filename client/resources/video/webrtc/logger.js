(function (X) {
    'use strict';

    function make(tag) {
        return function (message) {
            console.log('[' + tag + '] ' + message);
        };
    }

    X.log = {
        call: make('Call'),
        media: make('Media'),
        ice: make('ICE'),
        peer: make('Peer'),
        signal: make('Signal'),
        stats: make('Stats'),
        bridge: make('Bridge'),
        state: make('State'),
        ui: make('UI'),
        recorder: make('Recorder'),
        warn: function (tag, message) {
            if (message === undefined) {
                message = tag;
                tag = 'Call';
            }
            console.warn('[' + tag + '] ' + message);
        }
    };

    console.log('[Logger] loaded');
}(window.Xiaofu = window.Xiaofu || {}));
