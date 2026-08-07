$ErrorActionPreference = 'Stop'

$pageScript = Join-Path $PSScriptRoot '..\resources\video\video_call.js'
if (-not (Test-Path -LiteralPath $pageScript)) {
    throw "WebRTC 页面脚本不存在: $pageScript"
}

$content = Get-Content -LiteralPath $pageScript -Raw
$requiredFragments = @(
    'getUserMedia',
    'width: { ideal: 640 }',
    'height: { ideal: 480 }',
    'frameRate: { ideal: 30, max: 30 }',
    'audio: false',
    'RTCPeerConnection'
)

foreach ($fragment in $requiredFragments) {
    if (-not $content.Contains($fragment)) {
        throw "WebRTC 页面缺少必要配置: $fragment"
    }
}

Write-Host 'WebRTC page audit passed'
