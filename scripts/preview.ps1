[CmdletBinding()]
param(
    [ValidateSet('normal', 'warning', 'stone')][string]$State = 'normal',
    [ValidateSet('ring', 'bar')][string]$View = 'ring',
    [ValidateSet('compact', 'standard', 'expanded')][string]$Layout = 'compact',
    [ValidateRange(60, 140)][int]$Scale = 85,
    [switch]$WeeklyOnly,
    [switch]$ProAccount,
    [string]$Executable
)
$ErrorActionPreference = 'Stop'
if (-not $Executable) {
    foreach ($previewCandidate in @(
        (Join-Path $PSScriptRoot '..\work\ui-detail-build\ChatGPTCodexUsageMonitor.exe'),
        (Join-Path $PSScriptRoot '..\build\Release\ChatGPTCodexUsageMonitor.exe'),
        (Join-Path $PSScriptRoot '..\build\ChatGPTCodexUsageMonitor.exe')
    )) {
        if (Test-Path -LiteralPath $previewCandidate -PathType Leaf) {
            $Executable = $previewCandidate
            break
        }
    }
}
if (-not $Executable) { throw 'Build this branch first, or supply -Executable with the built application path.' }
$previewExecutable = (Resolve-Path -LiteralPath $Executable).Path
$previewEnvironment = @{
    MONITOR_DEMO_VIEW = $View
    MONITOR_DEMO_LAYOUT = $Layout
    MONITOR_DEMO_SCALE = [string]$Scale
    MONITOR_DEMO_WEEKLY_ONLY = [string][int]$WeeklyOnly.IsPresent
    MONITOR_DEMO_PRO_ACCOUNT = [string][int]$ProAccount.IsPresent
    MONITOR_DEMO_REMAINING = $(if ($State -eq 'normal') { '68' } elseif ($State -eq 'warning') { '34' } else { '0' })
    MONITOR_DEMO_FIVE_HOUR_REMAINING = $(if ($State -eq 'normal') { '86' } elseif ($State -eq 'warning') { '22' } else { '0' })
}
$previousEnvironment = @{}
try {
    foreach ($key in $previewEnvironment.Keys) {
        $previousEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        [Environment]::SetEnvironmentVariable($key, $previewEnvironment[$key], 'Process')
    }
    Start-Process -FilePath $previewExecutable -ArgumentList '--demo' -WindowStyle Hidden
} finally {
    foreach ($key in $previousEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($key, $previousEnvironment[$key], 'Process')
    }
}
Write-Host 'Preview opened with sample data. Its Settings menu can switch states and layouts.'
Write-Host 'If a preview is already open, it is restored without changing its current state.'
Write-Host 'The installed monitor and its saved settings are not modified.'
