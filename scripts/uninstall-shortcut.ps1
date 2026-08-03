[CmdletBinding()]
param(
    [switch]$RemoveConfig
)

$desktop = [Environment]::GetFolderPath('DesktopDirectory')
$shortcutPath = Join-Path $desktop 'ChatGPT with token.lnk'
if (Test-Path -LiteralPath $shortcutPath) {
    Remove-Item -LiteralPath $shortcutPath -Force
    Write-Host "已删除快捷方式：$shortcutPath"
} else {
    Write-Host '未找到本程序创建的桌面快捷方式。'
}

if ($RemoveConfig) {
    $configPath = Join-Path $env:LOCALAPPDATA 'ChatGPTCodexUsageMonitor'
    if (Test-Path -LiteralPath $configPath) {
        $resolved = (Resolve-Path -LiteralPath $configPath).Path
        $expected = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'ChatGPTCodexUsageMonitor'))
        if ($resolved -ne $expected) {
            throw "配置目录校验失败：$resolved"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
        Write-Host "已删除可选配置和日志：$resolved"
    }
}
