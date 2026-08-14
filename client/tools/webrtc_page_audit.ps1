$ErrorActionPreference = 'Stop'

$videoRoot = Join-Path $PSScriptRoot '..\resources\video'
$files = @{
    Entry = 'video_call.js'
    State = 'webrtc\state.js'
    Media = 'webrtc\media.js'
    Peer = 'webrtc\peer.js'
    Signal = 'webrtc\signaling.js'
    Ice = 'webrtc\ice.js'
}

$content = @{}
foreach ($name in $files.Keys) {
    $path = Join-Path $videoRoot $files[$name]
    if (-not (Test-Path -LiteralPath $path)) {
        throw "WebRTC 模块不存在: $($files[$name])"
    }
    $content[$name] = Get-Content -LiteralPath $path -Raw
}

$page = Get-Content -Raw -LiteralPath (Join-Path $videoRoot 'video_call.html')
foreach ($fragment in @('clamp(', 'aspect-ratio: 16 / 9', ':focus-visible', ':active',
                         '@media (max-width:', '@media (prefers-reduced-motion:',
                         '-webkit-line-clamp: 2')) {
    if (-not $page.Contains($fragment)) {
        throw "通话页缺少响应式/可访问性样式: $fragment"
    }
}

$required = @{
    Entry = @('startPreview', 'startOutgoingCall', 'applyRemoteSignal', 'stopCall')
    State = @('callToken', 'pendingCandidates', 'videoTransceiver', 'audioTransceiver')
    Media = @('getUserMedia', 'audio: false', 'reacquireCamera', 'startAudio', 'setCameraEnabled', 'setMicEnabled')
    Peer = @('RTCPeerConnection', "addTransceiver('video'", "addTransceiver('audio'", 'replaceTrack')
    Signal = @('createOffer', 'createAnswer', 'addIceCandidate', 'pendingCandidates')
    Ice = @('setIceServers', 'getTurnIceServers', 'iceTransportPolicy')
}

foreach ($module in $required.Keys) {
    foreach ($fragment in $required[$module]) {
        if (-not $content[$module].Contains($fragment)) {
            throw "$module 模块缺少必要机制: $fragment"
        }
    }
}

Write-Host 'WebRTC page audit passed'
