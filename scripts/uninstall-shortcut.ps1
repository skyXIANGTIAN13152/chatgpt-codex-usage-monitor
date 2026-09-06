[CmdletBinding()]
param(
    [switch]$RemoveConfig
)

$desktop = [Environment]::GetFolderPath('DesktopDirectory')
$shortcutPath = Join-Path $desktop 'ChatGPT with token.lnk'
if (Test-Path -LiteralPath $shortcutPath) {
    Remove-Item -LiteralPath $shortcutPath -Force
    Write-Host "Shortcut removed: $shortcutPath"
} else {
    Write-Host 'The desktop shortcut created by this application was not found.'
}

if ($RemoveConfig) {
    $configPath = Join-Path $env:LOCALAPPDATA 'ChatGPTCodexUsageMonitor'
    if (Test-Path -LiteralPath $configPath) {
        $resolved = (Resolve-Path -LiteralPath $configPath).Path
        $expected = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'ChatGPTCodexUsageMonitor'))
        if ($resolved -ne $expected) {
            throw "Configuration directory validation failed: $resolved"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
        Write-Host "Optional configuration and logs removed: $resolved"
    }
}
