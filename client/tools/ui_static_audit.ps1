$ErrorActionPreference = 'Stop'

$clientRoot = Split-Path -Parent $PSScriptRoot
$uiRoot = Join-Path $clientRoot 'ui'
$sourceRoot = Join-Path $clientRoot 'src'
$failures = New-Object System.Collections.Generic.List[string]

$checks = @(
    @{ Ui = 'LoginWidget.ui'; Width = 900; Height = 930; Names = @('editUser', 'editPass', 'btnLogin') },
    @{ Ui = 'RegisterWidget.ui'; Width = 900; Height = 930; Names = @('editUser', 'editMail', 'editPass', 'editPass2', 'btnRegister') },
    @{ Ui = 'ForgotPasswordWidget.ui'; Width = 900; Height = 930; Names = @('editUser', 'editNewPass', 'btnSend') },
    @{ Ui = 'MainWidget.ui'; Width = 1650; Height = 1000; Names = @('listContacts', 'listOffline', 'chatStack', 'settingsMask') },
    @{ Ui = 'ChatWidget.ui'; Width = 1650; Height = 1000; Names = @('convList', 'listMessages', 'editMessage', 'btnSend') },
    @{ Ui = 'CallWidget.ui'; Width = 1650; Height = 1000; Names = @('callStack', 'btnMic', 'btnCam', 'btnHangup', 'moreMenu') }
)

foreach ($check in $checks) {
    $path = Join-Path $uiRoot $check.Ui
    if (-not (Test-Path -LiteralPath $path)) {
        $failures.Add("missing UI file: $($check.Ui)")
        continue
    }

    [xml]$xml = Get-Content -Raw -LiteralPath $path
    $geometry = $xml.ui.widget.property | Where-Object { $_.name -eq 'geometry' } | Select-Object -First 1
    $width = [int]$geometry.rect.width
    $height = [int]$geometry.rect.height
    if ($width -ne $check.Width -or $height -ne $check.Height) {
        $failures.Add("$($check.Ui): expected $($check.Width)x$($check.Height), actual ${width}x${height}")
    }

    $allNames = @($xml.SelectNodes('//widget[@name] | //layout[@name] | //spacer[@name] | //action[@name]') |
        ForEach-Object { $_.name })
    $duplicateNames = @($allNames | Group-Object | Where-Object { $_.Count -gt 1 })
    foreach ($duplicate in $duplicateNames) {
        $failures.Add("$($check.Ui): duplicate object name '$($duplicate.Name)'")
    }
    foreach ($name in $check.Names) {
        if ($name -notin $allNames) {
            $failures.Add("$($check.Ui): missing object '$name'")
        }
    }
}

foreach ($authUi in @('LoginWidget.ui', 'RegisterWidget.ui', 'ForgotPasswordWidget.ui')) {
    $content = Get-Content -Raw -LiteralPath (Join-Path $uiRoot $authUi)
    foreach ($requiredStyle in @('Microsoft YaHei UI', ':pressed', ':disabled')) {
        if ($content -notmatch [regex]::Escape($requiredStyle)) {
            $failures.Add("${authUi}: missing style '$requiredStyle'")
        }
    }
}
$loginUiContent = Get-Content -Raw -LiteralPath (Join-Path $uiRoot 'LoginWidget.ui')
if ($loginUiContent -match '静态原型') {
    $failures.Add("LoginWidget.ui: stale static-prototype hint")
}

$todoFiles = @('MainWidget.cpp', 'ChatWidget.cpp', 'CallWidget.cpp')
foreach ($file in $todoFiles) {
    $path = Join-Path $sourceRoot $file
    $content = Get-Content -Raw -LiteralPath $path
    if ($content -notmatch '// TODO:[^\r\n]*[\u4e00-\u9fff]') {
        $failures.Add("${file}: missing Chinese // TODO comment")
    }
}

$mainWindowHeader = Get-Content -Raw -LiteralPath (Join-Path $sourceRoot 'MainWindow.h')
$mainWindowSource = Get-Content -Raw -LiteralPath (Join-Path $sourceRoot 'MainWindow.cpp')
$mainEntrySource = Get-Content -Raw -LiteralPath (Join-Path $sourceRoot 'main.cpp')
foreach ($fontToken in @('Microsoft YaHei UI', 'setFont')) {
    if ($mainEntrySource -notmatch [regex]::Escape($fontToken)) {
        $failures.Add("main.cpp: missing application font token '$fontToken'")
    }
}
foreach ($signature in @('showAuthPage', 'showWorkspacePage', 'kAuthWindowSize', 'kWorkspaceWindowSize')) {
    if ($mainWindowHeader -notmatch [regex]::Escape($signature)) {
        $failures.Add("MainWindow.h: missing '$signature'")
    }
}
foreach ($sizeLiteral in @('QSize(900, 930)', 'QSize(1650, 1000)')) {
    if ($mainWindowSource -notmatch [regex]::Escape($sizeLiteral)) {
        $failures.Add("MainWindow.cpp: missing '$sizeLiteral'")
    }
}
if ($mainWindowSource -notmatch [regex]::Escape('QColor("#C7C7CC")')) {
    $failures.Add('MainWindow.cpp: paintEvent must preserve the gray window border')
}
$mainWindowUiPath = Join-Path $uiRoot 'MainWindow.ui'
$mainWindowUi = Get-Content -Raw -LiteralPath $mainWindowUiPath
if ($mainWindowUi -notmatch [regex]::Escape('#MainWindow{background:#C7C7CC;}')) {
    $failures.Add('MainWindow.ui: missing 1px border background color')
}
[xml]$mainWindowXml = $mainWindowUi
$mainWindowRootLayout = $mainWindowXml.SelectSingleNode("//layout[@name='rootLayout']")
foreach ($marginName in @('leftMargin', 'topMargin', 'rightMargin', 'bottomMargin')) {
    $marginNode = $mainWindowRootLayout.SelectSingleNode("property[@name='$marginName']/number")
    if ($null -eq $marginNode -or [int]$marginNode.InnerText -ne 1) {
        $failures.Add("MainWindow.ui: rootLayout '$marginName' must be 1px")
    }
}

$mainWidgetHeader = Get-Content -Raw -LiteralPath (Join-Path $sourceRoot 'MainWidget.h')
$mainWidgetSource = Get-Content -Raw -LiteralPath (Join-Path $sourceRoot 'MainWidget.cpp')
foreach ($signature in @('appendLocalMessage')) {
    if ($mainWidgetHeader -notmatch [regex]::Escape($signature)) {
        $failures.Add("MainWidget.h: missing '$signature'")
    }
}
foreach ($behavior in @('setHidden', 'QTime::currentTime', 'scrollToBottom', 'settingsMask->setGeometry(rect())', 'settingsMask->raise()', 'setPixelSize(13)', 'bubble->setContentsMargins(14, 10, 14, 10)', 'horizontalAdvance', 'const int contentHeight')) {
    if ($mainWidgetSource -notmatch [regex]::Escape($behavior)) {
        $failures.Add("MainWidget.cpp: missing behavior '$behavior'")
    }
}
$mainWidgetUi = Get-Content -Raw -LiteralPath (Join-Path $uiRoot 'MainWidget.ui')
if ($mainWidgetUi -notmatch [regex]::Escape('Microsoft YaHei UI')) {
    $failures.Add("MainWidget.ui: missing Microsoft YaHei UI font")
}
if ($mainWidgetUi -notmatch '<item alignment="Qt::AlignHCenter">\s*<widget class="QLabel" name="avatarEmpty">') {
    $failures.Add("MainWidget.ui: empty-state avatar is not horizontally centered")
}
if ($mainWidgetUi -notmatch [regex]::Escape('QListWidget#listContacts::item,QListWidget#listOffline::item{padding:0px;')) {
    $failures.Add('MainWidget.ui: contact items still have duplicate padding')
}
if ($mainWidgetSource -notmatch [regex]::Escape('layout->setContentsMargins(16, 6, 16, 14)')) {
    $failures.Add('MainWidget.cpp: contact avatar is not shifted upward safely')
}
if ($mainWidgetUi -notmatch [regex]::Escape('QListWidget#listMessages::item{padding:0px;')) {
    $failures.Add('MainWidget.ui: listMessages still has duplicate item padding')
}

$chatHeader = Get-Content -Raw -LiteralPath (Join-Path $sourceRoot 'ChatWidget.h')
$chatSource = Get-Content -Raw -LiteralPath (Join-Path $sourceRoot 'ChatWidget.cpp')
foreach ($signature in @('appendLocalMessage', 'addMessageItem')) {
    if ($chatHeader -notmatch [regex]::Escape($signature)) {
        $failures.Add("ChatWidget.h: missing '$signature'")
    }
}
foreach ($behavior in @('QTime::currentTime', 'scrollToBottom', 'headName->setText', 'bubble->setContentsMargins(14, 10, 14, 10)', 'setPixelSize(13)', 'horizontalAdvance', 'item->setSizeHint')) {
    if ($chatSource -notmatch [regex]::Escape($behavior)) {
        $failures.Add("ChatWidget.cpp: missing behavior '$behavior'")
    }
}
$chatUi = Get-Content -Raw -LiteralPath (Join-Path $uiRoot 'ChatWidget.ui')
if ($chatUi -notmatch [regex]::Escape('Microsoft YaHei UI')) {
    $failures.Add("ChatWidget.ui: missing Microsoft YaHei UI font")
}
if ($chatUi -notmatch [regex]::Escape('QListWidget#listMessages::item{padding:0px;')) {
    $failures.Add('ChatWidget.ui: listMessages still has duplicate item padding')
}

$callHeader = Get-Content -Raw -LiteralPath (Join-Path $sourceRoot 'CallWidget.h')
$callSource = Get-Content -Raw -LiteralPath (Join-Path $sourceRoot 'CallWidget.cpp')
foreach ($state in @('microphoneEnabled', 'cameraEnabled', 'pipExpanded', 'recordingEnabled', 'speakerEnabled')) {
    if ($callHeader -notmatch [regex]::Escape($state)) {
        $failures.Add("CallWidget.h: missing state '$state'")
    }
}
foreach ($behavior in @('setProperty', 'unpolish', 'showFullScreen', 'backToMainWidget', 'Qt::WA_StyledBackground')) {
    if ($callSource -notmatch [regex]::Escape($behavior)) {
        $failures.Add("CallWidget.cpp: missing behavior '$behavior'")
    }
}
foreach ($dragBehavior in @(
    'eventFilter(QObject* watched, QEvent* event)',
    'clampPipPosition',
    'resetPipPosition',
    'pipPressGlobalPosition',
    'pipStartPosition',
    'pipDragging',
    'ignoreNextPipClick',
    'kPipDragThreshold = 6',
    'btnPip->installEventFilter(this)',
    'kPipMargin = 12',
    '清理未产生 clicked 的拖拽状态'
)) {
    if (($callHeader + $callSource) -notmatch [regex]::Escape($dragBehavior)) {
        $failures.Add("CallWidget: missing PiP drag behavior '$dragBehavior'")
    }
}
foreach ($behavior in @('btnMore->mapTo(this', 'moreMenu->move', 'qBound', '- 26')) {
    if ($callSource -notmatch [regex]::Escape($behavior)) {
        $failures.Add("CallWidget.cpp: missing menu positioning '$behavior'")
    }
}
foreach ($behavior in @('startDemoCall', 'demoCallSerial')) {
    if ($callHeader -notmatch [regex]::Escape($behavior)) {
        $failures.Add("CallWidget.h: missing call reset '$behavior'")
    }
}
$callUi = Get-Content -Raw -LiteralPath (Join-Path $uiRoot 'CallWidget.ui')
if ($callUi -notmatch [regex]::Escape('Microsoft YaHei UI')) {
    $failures.Add("CallWidget.ui: missing Microsoft YaHei UI font")
}
if ($callUi -notmatch [regex]::Escape('QWidget#CallWidget,QWidget#callPage')) {
    $failures.Add('CallWidget.ui: promoted callPage is missing dark background selector')
}
foreach ($avatarSelector in @('#avatarRinging{background:#10B981', '#avatarIncall{background:#10B981')) {
    if ($callUi -notmatch [regex]::Escape($avatarSelector)) {
        $failures.Add("CallWidget.ui: avatar color does not match HTML '$avatarSelector'")
    }
}
if ($callUi -match 'rgba\([^\)]*,0\.[0-9]+\)') {
    $failures.Add('CallWidget.ui: Qt-incompatible decimal rgba alpha remains')
}
foreach ($centeredControl in @('statusPill', 'avatarRinging', 'btnCancelCall', 'avatarIncall')) {
    $pattern = '<item alignment="Qt::AlignHCenter">\s*<widget class="[^"]+" name="' + [regex]::Escape($centeredControl) + '"'
    if ($callUi -notmatch $pattern) {
        $failures.Add("CallWidget.ui: '$centeredControl' is not horizontally centered")
    }
}
$resourceSource = Get-Content -Raw -LiteralPath (Join-Path $clientRoot 'resources.qrc')
$iconResources = @{
    'icon-mic-new.png' = '../UI/icons/maikefeng.png'
    'icon-cam-new.png' = '../UI/icons/shexiangtou.png'
    'icon-more.png' = '../UI/icons/gengduo.png'
}
foreach ($alias in $iconResources.Keys) {
    $resourcePattern = '<file alias="' + [regex]::Escape($alias) + '">' +
        [regex]::Escape($iconResources[$alias]) + '</file>'
    if ($resourceSource -notmatch $resourcePattern) {
        $failures.Add("resources.qrc: missing call control icon '$alias'")
    }
}
[xml]$callXml = $callUi
$hangupButton = $callXml.SelectSingleNode("//widget[@name='btnHangup']")
$hangupText = $hangupButton.SelectSingleNode("property[@name='text']/string").InnerText
if ($hangupText -ne '') {
    $failures.Add("CallWidget.ui: 'btnHangup' must be icon-only")
}
$callButtonIcons = @{
    btnMic = ':/icons/icon-mic-new.png'
    btnCam = ':/icons/icon-cam-new.png'
    btnMore = ':/icons/icon-more.png'
}
foreach ($buttonName in $callButtonIcons.Keys) {
    $button = $callXml.SelectSingleNode("//widget[@name='$buttonName']")
    $buttonText = $button.SelectSingleNode("property[@name='text']/string").InnerText
    $buttonIcon = $button.SelectSingleNode("property[@name='icon']/iconset/normaloff").InnerText
    $iconWidth = [int]$button.SelectSingleNode("property[@name='iconSize']/size/width").InnerText
    $iconHeight = [int]$button.SelectSingleNode("property[@name='iconSize']/size/height").InnerText
    if ($buttonText -ne '') {
        $failures.Add("CallWidget.ui: '$buttonName' must be icon-only")
    }
    if ($buttonIcon -ne $callButtonIcons[$buttonName]) {
        $failures.Add("CallWidget.ui: '$buttonName' has wrong icon")
    }
    if ($iconWidth -ne 22 -or $iconHeight -ne 22) {
        $failures.Add("CallWidget.ui: '$buttonName' icon must be 22x22")
    }
}

if ($failures.Count -gt 0) {
    foreach ($failure in $failures) {
        Write-Host "[FAIL] $failure" -ForegroundColor Red
    }
    exit 1
}

foreach ($check in $checks) {
    Write-Host "[PASS] $($check.Ui)" -ForegroundColor Green
}
Write-Host '[PASS] Chinese TODO boundaries' -ForegroundColor Green
Write-Host '[PASS] MainWindow centralized sizing' -ForegroundColor Green
Write-Host '[PASS] MainWidget local interactions' -ForegroundColor Green
Write-Host '[PASS] ChatWidget local interactions' -ForegroundColor Green
Write-Host '[PASS] CallWidget local state machine' -ForegroundColor Green
exit 0
