param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$ProfileDirectory,
    [Parameter(Mandatory=$true)][string]$WorkingDirectory
)
$ErrorActionPreference = 'Stop'

# Called only after the user clicks the official first-setup action.
# Keep paths as data; no user value is inserted into executable shell code.
if (-not [System.IO.File]::Exists($Executable)) { exit 202 }
if (-not [System.IO.Directory]::Exists($ProfileDirectory)) { exit 203 }
if (-not [System.IO.Directory]::Exists($WorkingDirectory)) { exit 204 }
$env:CLAUDE_CONFIG_DIR = $ProfileDirectory
$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = $Executable
$startInfo.WorkingDirectory = $WorkingDirectory
$startInfo.UseShellExecute = $true
$startInfo.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Normal
# /usage is a documented local command, never an inference prompt.
$startInfo.Arguments = '--safe-mode --ax-screen-reader /usage'
try {
    $officialProcess = [System.Diagnostics.Process]::Start($startInfo)
    $officialProcess.WaitForExit()
    exit $officialProcess.ExitCode
} catch {
    exit 205
}
