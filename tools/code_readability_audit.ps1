$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$failures = New-Object System.Collections.Generic.List[string]

$forbiddenTokens = @{
    'server/xiaofu_server/src/main.cpp' = @('conns', 'makeResp')
    'server/xiaofu_server/src/db/DbManager.cpp' = @('int rc', 'char* err', 'sqlite3_stmt* stmt', 'const unsigned char* col')
    'server/xiaofu_server/src/db/PasswordHasher.cpp' = @('hProv', 'hHash', 'std::stringstream ss')
    'server/xiaofu_server/src/net/Connection.h' = @('messageCb', 'closeCb', 'inBuf')
    'server/xiaofu_server/src/net/EventLoopWin.cpp' = @('timerCb', 'pfds', 'std::vector<int> ids')
    'server/xiaofu_server/src/net/TcpServer.h' = @('listenSock', 'acceptCb')
    'server/xiaofu_server/src/handler/JoinHandler.h' = @('fdToUser', 'kickCb')
    'client/src/network/NetworkManager.cpp' = @('quint32 len', 'QJsonObject obj', 'QJsonParseError err')
}

foreach ($relativePath in $forbiddenTokens.Keys) {
    $path = Join-Path $projectRoot $relativePath
    $content = Get-Content -Raw -LiteralPath $path
    foreach ($token in $forbiddenTokens[$relativePath]) {
        $tokenPattern = '(?<![A-Za-z0-9_])' + [regex]::Escape($token) + '(?![A-Za-z0-9_])'
        if ($content -match $tokenPattern) {
            $failures.Add("${relativePath}: unclear identifier remains '$token'")
        }
    }
}

$requiredComments = @{
    'server/xiaofu_server/src/db/PasswordHasher.cpp' = '密码加密函数'
    'server/xiaofu_server/src/net/Connection.cpp' = '解析长度头并拆分完整消息'
    'server/xiaofu_server/src/main.cpp' = '服务端启动流程'
}

foreach ($relativePath in $requiredComments.Keys) {
    $content = Get-Content -Raw -LiteralPath (Join-Path $projectRoot $relativePath)
    if ($content -notmatch [regex]::Escape($requiredComments[$relativePath])) {
        $failures.Add("${relativePath}: missing Chinese responsibility comment '$($requiredComments[$relativePath])'")
    }
}

$serverMain = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'server/xiaofu_server/src/main.cpp')
$deferredDeletePattern = 'connection->onReadable\(\);\s*if \(connection->closed\(\)\)\s*removeConnection\(fd\);'
if ($serverMain -notmatch $deferredDeletePattern) {
    $failures.Add('server/xiaofu_server/src/main.cpp: closed connection must be deleted after onReadable returns')
}

if ($failures.Count -gt 0) {
    foreach ($failure in $failures) {
        Write-Host "[FAIL] $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host '[PASS] client/server readability audit' -ForegroundColor Green
exit 0
