# Downloads the PackSquash binary that ships alongside the launcher.
#
# PackSquash (https://github.com/ComunidadAylas/PackSquash, AGPL-3.0) shrinks
# resource packs before they are uploaded to a shared instance. We call it as a
# separate program, so its license stays its own; the launcher just runs it.
#
# The binary is not committed - run this once before packaging a release, or
# any time PACKSQUASH_VERSION below changes.

param([switch]$Force)

$ErrorActionPreference = "Stop"

$PACKSQUASH_VERSION = "v0.4.1"
$PACKSQUASH_ASSET = "packsquash.exe-x86_64-pc-windows-gnu.zip"
# sha256 of the release zip, verified on 2026-08-17
$PACKSQUASH_SHA256 = "88ACFE76003D188BAF2421D18F0E818F19157504CBD058857D62C8D9C3A596E9"

$toolsDir = Join-Path $PSScriptRoot "packsquash"
$exePath = Join-Path $toolsDir "packsquash.exe"
$stampPath = Join-Path $toolsDir "version.txt"

if ((Test-Path $exePath) -and (Test-Path $stampPath) -and -not $Force) {
    if ((Get-Content $stampPath -Raw).Trim() -eq $PACKSQUASH_VERSION) {
        Write-Host "PackSquash $PACKSQUASH_VERSION already present."
        exit 0
    }
}

New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
$zipPath = Join-Path $toolsDir "download.zip"
$url = "https://github.com/ComunidadAylas/PackSquash/releases/download/$PACKSQUASH_VERSION/$PACKSQUASH_ASSET"

Write-Host "Downloading PackSquash $PACKSQUASH_VERSION..."
Invoke-WebRequest -Uri $url -Headers @{ "User-Agent" = "PortalLauncher-Build" } -OutFile $zipPath

$hash = (Get-FileHash $zipPath -Algorithm SHA256).Hash
if ($hash -ne $PACKSQUASH_SHA256) {
    Remove-Item $zipPath -Force
    throw "PackSquash download hash mismatch. Expected $PACKSQUASH_SHA256, got $hash"
}

Expand-Archive -Path $zipPath -DestinationPath $toolsDir -Force
Remove-Item $zipPath -Force
Set-Content -Path $stampPath -Value $PACKSQUASH_VERSION -Encoding ascii -NoNewline

if (-not (Test-Path $exePath)) {
    throw "PackSquash archive did not contain packsquash.exe"
}
Write-Host "PackSquash $PACKSQUASH_VERSION ready at $exePath"
