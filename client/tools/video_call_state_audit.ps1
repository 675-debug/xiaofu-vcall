$ErrorActionPreference = 'Stop'

$headerPath = Join-Path $PSScriptRoot '..\src\video\VideoCallController.h'
$sourcePath = Join-Path $PSScriptRoot '..\src\video\VideoCallController.cpp'
$callWidgetPath = Join-Path $PSScriptRoot '..\src\CallWidget.cpp'
if (-not (Test-Path -LiteralPath $headerPath) -or -not (Test-Path -LiteralPath $sourcePath) -or
    -not (Test-Path -LiteralPath $callWidgetPath)) {
    throw 'VideoCallController 文件不存在'
}

$header = Get-Content -LiteralPath $headerPath -Raw
$source = Get-Content -LiteralPath $sourcePath -Raw
$callWidgetSource = Get-Content -LiteralPath $callWidgetPath -Raw
foreach ($state in @('Idle', 'OutgoingRinging', 'IncomingRinging', 'Connecting', 'InCall', 'Ending')) {
    if (-not $header.Contains($state)) {
        throw "通话状态机缺少状态: $state"
    }
}

if ($header -notmatch 'receiveIncomingCall\s*\(\s*const QString&\s+\w+\s*,\s*const QString&\s+callId\s*\)') {
    throw '来电入口必须接收服务端下发的 callId'
}

if ($callWidgetSource -notmatch 'signal\.value\(QStringLiteral\("callId"\)\)\.toString\(\)' -or
    $callWidgetSource -notmatch 'receiveIncomingCall\s*\(\s*peerName\s*,\s*callId\s*\)') {
    throw 'CallWidget 未把 call_request 的 callId 原样传给控制器'
}

foreach ($endSignal in @('call_cancel', 'call_reject', 'call_hangup', 'peer_disconnected', 'call_ended')) {
    if (-not $source.Contains($endSignal)) {
        throw "通话控制器缺少远端结束事件: $endSignal"
    }
}

if ($source -notmatch 'call_hangup[\s\S]{0,1000}stopCall\(\)') {
    throw '收到 call_hangup 后未停止 WebRTC 媒体资源'
}

if (-not $callWidgetSource.Contains('ui->callStack->setCurrentIndex(1);') -or
    -not $callWidgetSource.Contains('QTimer::singleShot(0, this, [this]() {') -or
    -not $callWidgetSource.Contains('updateWebRtcGeometry();')) {
    throw '进入通话页后未在布局完成时同步 WebRTC 视频区域尺寸'
}

Write-Host 'Video call state audit passed'
