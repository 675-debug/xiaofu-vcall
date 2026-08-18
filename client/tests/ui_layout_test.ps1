$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw "UI layout regression: $Message"
    }
}

$clientRoot = Split-Path -Parent $PSScriptRoot
$mainWindowSource = Get-Content -Raw -LiteralPath (Join-Path $clientRoot 'src/MainWindow.cpp')
$mainWidgetSource = Get-Content -Raw -LiteralPath (Join-Path $clientRoot 'src/MainWidget.cpp')
$mainUi = Get-Content -Raw -LiteralPath (Join-Path $clientRoot 'ui/MainWidget.ui')
$chatUi = Get-Content -Raw -LiteralPath (Join-Path $clientRoot 'ui/ChatWidget.ui')

Assert-True ($mainWindowSource -notmatch 'clearMinSizeRecursive') `
    'MainWindow must not erase child minimum sizes declared by Designer.'

foreach ($entry in @(
    @{ Name = 'MainWidget input row'; Xml = $mainUi; Widget = 'inputRow'; Height = 72 },
    @{ Name = 'ChatWidget input row'; Xml = $chatUi; Widget = 'inputRow'; Height = 72 },
    @{ Name = 'MainWidget message editor'; Xml = $mainUi; Widget = 'editMessage'; Height = 44 },
    @{ Name = 'ChatWidget message editor'; Xml = $chatUi; Widget = 'editMessage'; Height = 44 }
)) {
    $pattern = 'name="' + $entry.Widget + '"[\s\S]*?<property name="minimumSize">[\s\S]*?<height>' + $entry.Height + '</height>'
    Assert-True ($entry.Xml -match $pattern) "$($entry.Name) must keep a minimum height of $($entry.Height) px."
}

Assert-True ($mainUi -match '#chatAvatar\{border-radius:22px') `
    'Main chat avatar must be circular at 44x44.'
Assert-True ($chatUi -match '#headAvatar\{border-radius:22px') `
    'Standalone chat avatar must be circular at 44x44.'
Assert-True ($mainWidgetSource -match 'setSizeHint\(QSize\(0, 64\)\)') `
    'Contact rows must keep a readable 64 px height.'

Write-Output 'UI layout checks passed.'
