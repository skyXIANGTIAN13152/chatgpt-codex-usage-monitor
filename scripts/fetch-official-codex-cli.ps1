[CmdletBinding()]
param(
    [string]$DestinationDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist')
)

$ErrorActionPreference = 'Stop'

function Get-Sha256([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $algorithm = [System.Security.Cryptography.SHA256]::Create()
        try {
            $hash = $algorithm.ComputeHash($stream)
            return [System.BitConverter]::ToString($hash).Replace('-', '').ToLowerInvariant()
        } finally {
            $algorithm.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

$version = '0.146.0'
$archiveSha256 = '4781b618fa3a16d91c892f8a1e2c82625f9286f9bb944a5690ba727c84fc5729'
$binarySha256 = 'bc343ba420dc2e2e9f59e6fc5e5bf0aae1cd8c771fc319665241fc9c0271fddb'
$archiveUrl = 'https://github.com/openai/codex/releases/download/rust-v0.146.0/codex-x86_64-pc-windows-msvc.exe.zip'
$licenseUrl = 'https://raw.githubusercontent.com/openai/codex/rust-v0.146.0/LICENSE'
$noticeUrl = 'https://raw.githubusercontent.com/openai/codex/rust-v0.146.0/NOTICE'
$licenseSha256 = 'd17f227e4df5da1600391338865ce0f3055211760a36688f816941d58232d8dc'
$noticeSha256 = '9d71575ecfd9a843fc1677b0efb08053c6ba9fd686a0de1a6f5382fd3c220915'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cacheDirectory = Join-Path $projectRoot 'work\official-codex-cli'
$archivePath = Join-Path $cacheDirectory 'codex-x86_64-pc-windows-msvc.exe.zip'
$extractedPath = Join-Path $cacheDirectory 'codex-x86_64-pc-windows-msvc.exe'
$licensePath = Join-Path $cacheDirectory 'LICENSE'
$noticePath = Join-Path $cacheDirectory 'NOTICE'

New-Item -ItemType Directory -Force -Path $cacheDirectory, $DestinationDirectory | Out-Null

if (-not (Test-Path -LiteralPath $archivePath)) {
    Write-Host "正在从 OpenAI 官方 GitHub Release 下载 Codex CLI $version……"
    Invoke-WebRequest -UseBasicParsing -Uri $archiveUrl -OutFile $archivePath
}

$actualArchiveHash = Get-Sha256 $archivePath
if ($actualArchiveHash -ne $archiveSha256) {
    throw "Codex CLI 压缩包校验失败。实际 SHA-256：$actualArchiveHash"
}

if (-not (Test-Path -LiteralPath $extractedPath)) {
    Expand-Archive -LiteralPath $archivePath -DestinationPath $cacheDirectory -Force
}

$actualBinaryHash = Get-Sha256 $extractedPath
if ($actualBinaryHash -ne $binarySha256) {
    throw "Codex CLI 可执行文件校验失败。实际 SHA-256：$actualBinaryHash"
}

Copy-Item -LiteralPath $extractedPath -Destination (Join-Path $DestinationDirectory 'codex.exe') -Force

foreach ($legalFile in @(
    @{ Path = $licensePath; Url = $licenseUrl; Hash = $licenseSha256; Output = 'Codex-CLI-LICENSE.txt' },
    @{ Path = $noticePath; Url = $noticeUrl; Hash = $noticeSha256; Output = 'Codex-CLI-NOTICE.txt' }
)) {
    if (-not (Test-Path -LiteralPath $legalFile.Path)) {
        Invoke-WebRequest -UseBasicParsing -Uri $legalFile.Url -OutFile $legalFile.Path
    }
    $actualLegalHash = Get-Sha256 $legalFile.Path
    if ($actualLegalHash -ne $legalFile.Hash) {
        throw "Codex CLI 许可证文件校验失败：$($legalFile.Output)"
    }
    Copy-Item -LiteralPath $legalFile.Path -Destination (Join-Path $DestinationDirectory $legalFile.Output) -Force
}

Write-Host "已打包官方 Codex CLI $version（SHA-256 已核对）。"
