$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$serverExe = Join-Path $projectRoot 'server\build\xiaofu-server.exe'
$utf8 = [System.Text.Encoding]::UTF8
$serverProcess = $null
$clients = @()

function Read-Exact([System.IO.Stream]$stream, [byte[]]$buffer) {
    $offset = 0
    while ($offset -lt $buffer.Length) {
        $received = $stream.Read($buffer, $offset, $buffer.Length - $offset)
        if ($received -le 0) { throw 'socket closed while reading frame' }
        $offset += $received
    }
}

function Read-JsonFrame([System.Net.Sockets.TcpClient]$client) {
    $stream = $client.GetStream()
    [byte[]]$header = New-Object byte[] 4
    Read-Exact $stream $header
    $bodyLength = (([int]$header[0] -shl 24) -bor ([int]$header[1] -shl 16) -bor ([int]$header[2] -shl 8) -bor [int]$header[3])
    [byte[]]$body = New-Object byte[] $bodyLength
    Read-Exact $stream $body
    $bodyText = $utf8.GetString($body)
    try {
        return ($bodyText | ConvertFrom-Json)
    } catch {
        Write-Host "frame declared bytes=$bodyLength actual bytes=$($body.Length)" -ForegroundColor Red
        Write-Host $bodyText -ForegroundColor Red
        throw
    }
}

function Send-JsonFrame([System.Net.Sockets.TcpClient]$client, [hashtable]$message) {
    $payload = $utf8.GetBytes(($message | ConvertTo-Json -Compress))
    $frame = New-Object byte[] (4 + $payload.Length)
    $frame[0] = ($payload.Length -shr 24) -band 0xFF
    $frame[1] = ($payload.Length -shr 16) -band 0xFF
    $frame[2] = ($payload.Length -shr 8) -band 0xFF
    $frame[3] = $payload.Length -band 0xFF
    [System.Buffer]::BlockCopy($payload, 0, $frame, 4, $payload.Length)
    $stream = $client.GetStream()
    $stream.Write($frame, 0, $frame.Length)
    return Read-JsonFrame $client
}

function Require([bool]$condition, [string]$name) {
    if (-not $condition) { throw "integration assertion failed: $name" }
    Write-Host "PASS: $name"
}

try {
    if (-not (Test-Path -LiteralPath $serverExe)) { throw "server executable missing: $serverExe" }
    $serverProcess = Start-Process -FilePath $serverExe -WorkingDirectory (Split-Path $serverExe) -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 1
    $serverProcess.Refresh()
    if ($serverProcess.HasExited) { throw "server exited early: $($serverProcess.ExitCode)" }

    $runId = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $clientCount = 5
    $users = @()
    for ($index = 0; $index -lt $clientCount; $index++) {
        $users += "chat_user_${runId}_$index"
        $clients += [System.Net.Sockets.TcpClient]::new('127.0.0.1', 9000)
    }

    for ($index = 0; $index -lt $clientCount; $index++) {
        $username = $users[$index]
        Require ((Send-JsonFrame $clients[$index] @{type='register'; username=$username; email="$username@example.com"; password='Chat123'; nickname="测试用户$index"; avatarSeed=$index}).code -eq 0) "register client $index"
        Require ((Send-JsonFrame $clients[$index] @{type='login'; username=$username; password='Chat123'}).code -eq 0) "login client $index"
    }

    for ($index = 0; $index -lt $clientCount; $index++) {
        Require ((Send-JsonFrame $clients[$index] @{type='join'; username=$users[$index]}).code -eq 0) "join client $index"
        for ($receiverIndex = 0; $receiverIndex -le $index; $receiverIndex++) {
            $presence = Read-JsonFrame $clients[$receiverIndex]
            Require ($presence.type -eq 'presence_push' -and $presence.username -eq $users[$index] -and $presence.online) "client $receiverIndex receives client $index online state"
        }
    }

    for ($index = 0; $index -lt $clientCount; $index++) {
        $nextIndex = ($index + 1) % $clientCount
        Require ((Send-JsonFrame $clients[$index] @{type='add_contact'; username=$users[$nextIndex]}).code -eq 0) "client $index adds next contact"
        $contacts = Send-JsonFrame $clients[$index] @{type='contacts'}
        Require ($contacts.type -eq 'contacts_resp' -and $contacts.contacts.Count -eq 1 -and $contacts.contacts[0].username -eq $users[$nextIndex] -and $contacts.contacts[0].online) "client $index loads online contact"
    }

    for ($index = 0; $index -lt $clientCount; $index++) {
        $nextIndex = ($index + 1) % $clientCount
        $content = "五客户端中文消息 $index"
        $chatResponse = Send-JsonFrame $clients[$index] @{type='chat'; to=$users[$nextIndex]; content=$content}
        Require ($chatResponse.type -eq 'chat_resp' -and $chatResponse.online -eq $true) "client $index routes online message"
        $chatPush = Read-JsonFrame $clients[$nextIndex]
        Require ($chatPush.type -eq 'chat_push' -and $chatPush.message.content -eq $content) "client $nextIndex receives routed message"
    }

    $offlineIndex = $clientCount - 1
    $clients[$offlineIndex].Close()
    $clients[$offlineIndex] = $null
    Start-Sleep -Milliseconds 300
    for ($receiverIndex = 0; $receiverIndex -lt $offlineIndex; $receiverIndex++) {
        $presence = Read-JsonFrame $clients[$receiverIndex]
        Require ($presence.type -eq 'presence_push' -and $presence.username -eq $users[$offlineIndex] -and -not $presence.online) "client $receiverIndex receives offline state"
    }

    $senderIndex = $offlineIndex - 1
    $offlineResponse = Send-JsonFrame $clients[$senderIndex] @{type='chat'; to=$users[$offlineIndex]; content='离线后也要保存'}
    Require ($offlineResponse.type -eq 'chat_resp' -and $offlineResponse.online -eq $false) 'offline chat is stored'

    $clients[$offlineIndex] = [System.Net.Sockets.TcpClient]::new('127.0.0.1', 9000)
    Require ((Send-JsonFrame $clients[$offlineIndex] @{type='login'; username=$users[$offlineIndex]; password='Chat123'}).code -eq 0) 'relogin offline client'
    Require ((Send-JsonFrame $clients[$offlineIndex] @{type='join'; username=$users[$offlineIndex]}).code -eq 0) 'rejoin offline client'
    for ($receiverIndex = 0; $receiverIndex -lt $clientCount; $receiverIndex++) {
        $presence = Read-JsonFrame $clients[$receiverIndex]
        Require ($presence.type -eq 'presence_push' -and $presence.username -eq $users[$offlineIndex] -and $presence.online) "client $receiverIndex receives reconnected state"
    }
    $history = Send-JsonFrame $clients[$offlineIndex] @{type='history'; peer=$users[$senderIndex]}
    Require ($history.type -eq 'history_resp' -and $history.messages.Count -ge 2) 'offline history is loaded after reconnect'

    $deleteResponse = Send-JsonFrame $clients[$offlineIndex] @{type='delete_chat'; peer=$users[$senderIndex]}
    Require ($deleteResponse.code -eq 0 -and $deleteResponse.peer -eq $users[$senderIndex]) 'delete current conversation'
    Require ((Send-JsonFrame $clients[$offlineIndex] @{type='history'; peer=$users[$senderIndex]}).messages.Count -eq 0) 'deleted conversation is empty'

    Require ((Send-JsonFrame $clients[0] @{type='chat'; to=$users[1]; content='用于清空测试'}).code -eq 0) 'store message before clear all'
    $null = Read-JsonFrame $clients[1]
    Require ((Send-JsonFrame $clients[1] @{type='clear_chats'}).code -eq 0) 'clear all chat records'
    Require ((Send-JsonFrame $clients[1] @{type='history'; peer=$users[0]}).messages.Count -eq 0) 'all chat records are empty'
    Write-Host 'ALL CHAT INTEGRATION PASS'
}
finally {
    foreach ($client in $clients) {
        if ($client) { $client.Close() }
    }
    if ($serverProcess -and -not $serverProcess.HasExited) {
        Stop-Process -Id $serverProcess.Id
        $serverProcess.WaitForExit()
    }
}
