[CmdletBinding()]
param(
    [string]$ExePath
)

if ([string]::IsNullOrWhiteSpace($ExePath)) {
    $ExePath = Join-Path $PSScriptRoot 'ChatGPTCodexUsageMonitor.exe'
}

$resolvedExe = (Resolve-Path -LiteralPath $ExePath -ErrorAction Stop).Path
$desktop = [Environment]::GetFolderPath('DesktopDirectory')
$shortcutPath = Join-Path $desktop 'ChatGPT with token.lnk'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $resolvedExe
$shortcut.Arguments = '--launcher'
$shortcut.WorkingDirectory = Split-Path -Parent $resolvedExe
$shortcut.IconLocation = "$resolvedExe,0"
$shortcut.Description = 'Launch ChatGPT with the Codex usage monitor'
$shortcut.Save()

Write-Host "Shortcut created: $shortcutPath"
