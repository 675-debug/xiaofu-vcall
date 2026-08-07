param(
    [string]$SourcePath = (Join-Path $PSScriptRoot "..\src\main.cpp")
)

$source = Get-Content -Raw $SourcePath
$requiredTokens = @(
    'call_request',
    'call_accept',
    'call_reject',
    'call_hangup',
    'webrtc_offer',
    'webrtc_answer',
    'ice_candidate',
    'call_signal_resp',
    'call signal relayed'
)

foreach ($token in $requiredTokens) {
    if (-not $source.Contains($token)) {
        throw "Missing video signalling token: $token"
    }
}

Write-Host "Video signal route audit passed"
