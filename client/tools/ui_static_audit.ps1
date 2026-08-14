$ErrorActionPreference = 'Stop'

$clientRoot = Split-Path -Parent $PSScriptRoot
$uiRoot = Join-Path $clientRoot 'ui'
$failures = New-Object System.Collections.Generic.List[string]
$checks = @(
    @{ File = 'MainWindow.ui'; Names = @('stackMain', 'mainPage', 'loginPage', 'registerPage', 'forgotPage', 'chatPage', 'callPage') },
    @{ File = 'LoginWidget.ui'; Names = @('editUser', 'editPass', 'btnLogin') },
    @{ File = 'RegisterWidget.ui'; Names = @('editUser', 'editNickname', 'editMail', 'editPass', 'editPass2', 'btnRegister') },
    @{ File = 'ForgotPasswordWidget.ui'; Names = @('editUser', 'editNewPass', 'btnSend') },
    @{ File = 'MainWidget.ui'; Names = @('listContacts', 'listOffline', 'chatStack', 'avatarEmpty', 'listMessages', 'settingsMask', 'addContactMask', 'editAddContact', 'friendRequestMask', 'friendRequestDescription') },
    @{ File = 'ChatWidget.ui'; Names = @('convList', 'listMessages', 'editMessage', 'btnSend') },
    @{ File = 'CallWidget.ui'; Names = @('callStack', 'viewRinging', 'viewIncall', 'btnPip') }
)

foreach ($check in $checks) {
    $path = Join-Path $uiRoot $check.File
    if (-not (Test-Path -LiteralPath $path)) {
        $failures.Add("missing UI file: $($check.File)")
        continue
    }
    try {
        [xml]$xml = Get-Content -Raw -LiteralPath $path
    } catch {
        $failures.Add("$($check.File): invalid XML: $($_.Exception.Message)")
        continue
    }
    $names = @($xml.SelectNodes('//widget[@name] | //layout[@name] | //spacer[@name] | //action[@name]') |
        ForEach-Object { $_.GetAttribute('name') })
    foreach ($duplicate in @($names | Group-Object | Where-Object Count -gt 1)) {
        $failures.Add("$($check.File): duplicate object name '$($duplicate.Name)'")
    }
    foreach ($name in $check.Names) {
        if ($name -notin $names) {
            $failures.Add("$($check.File): missing object '$name'")
        }
    }
}

foreach ($authUi in @('LoginWidget.ui', 'RegisterWidget.ui', 'ForgotPasswordWidget.ui')) {
    $text = Get-Content -Raw -LiteralPath (Join-Path $uiRoot $authUi)
    foreach ($state in @(':hover', ':pressed', ':disabled')) {
        if (-not $text.Contains($state)) {
            $failures.Add("${authUi}: missing button state '$state'")
        }
    }
}

$resourceText = Get-Content -Raw -LiteralPath (Join-Path $clientRoot 'resources.qrc')
foreach ($resource in @('resources/icons/maikefeng.png', 'resources/icons/shexiangtou.png', 'resources/icons/gengduo.png')) {
    if (-not $resourceText.Contains($resource)) {
        $failures.Add("resources.qrc: missing current icon resource '$resource'")
    }
}

$mainWindowText = Get-Content -Raw -LiteralPath (Join-Path $clientRoot 'src\MainWindow.cpp')
foreach ($fragment in @('kWorkspaceWindowSize = QSize(1240, 760)', 'availableGeometry()', 'boundedWindowSize')) {
    if (-not $mainWindowText.Contains($fragment)) {
        $failures.Add("MainWindow.cpp: missing logical DPI sizing '$fragment'")
    }
}
if ($mainWindowText.Contains('physical.width() / dpr')) {
    $failures.Add('MainWindow.cpp: still converts fixed physical size through DPR')
}

$asrConfig = Join-Path $clientRoot 'asr.url'
if (-not (Test-Path -LiteralPath $asrConfig)) {
    $failures.Add('missing client/asr.url')
} elseif ((Get-Content -Raw -LiteralPath $asrConfig).Trim() -ne 'ws://101.33.235.237:10095') {
    $failures.Add('client/asr.url: unexpected endpoint')
}

if ($failures.Count -gt 0) {
    foreach ($failure in $failures) {
        Write-Host "[FAIL] $failure" -ForegroundColor Red
    }
    exit 1
}

foreach ($check in $checks) {
    Write-Host "[PASS] $($check.File)" -ForegroundColor Green
}
Write-Host '[PASS] UI contracts and resource paths' -ForegroundColor Green
