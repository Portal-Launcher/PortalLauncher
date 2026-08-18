# Checks that an update archive can safely replace an existing installation.
#
#   powershell -File tools/verify-update-archive.ps1 -ZipPath dist/PortalLauncher-Windows-MSVC-1.0.5.zip
#
# The updater installs exactly what manifest.txt lists, so the manifest and the
# zip have to agree, and the files the launcher cannot start without have to be
# in both. Called by package-release.ps1 and again by publish-release.ps1.

param([Parameter(Mandatory = $true)][string]$ZipPath)

$ErrorActionPreference = "Stop"

# Compress-Archive on PowerShell 5.1 stores backslash separators, so entry names
# are normalised to the forward slashes that manifest.txt uses.
$slash = [char]92
$required = @("manifest.txt", "prismlauncher.exe", "prismlauncher_updater.exe", "Qt6Core.dll", "platforms/qwindows.dll")

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead((Resolve-Path $ZipPath))
try {
    # Entries with an empty Name are directories.
    $entries = @($archive.Entries | Where-Object { $_.Name } | ForEach-Object { $_.FullName.Replace($slash, '/') })
    $manifestEntry = $archive.Entries | Where-Object { $_.FullName.Replace($slash, '/') -eq "manifest.txt" } | Select-Object -First 1
    if (-not $manifestEntry) { throw "$ZipPath has no manifest.txt, so the updater would have to guess at the file list" }
    $reader = New-Object IO.StreamReader($manifestEntry.Open(), [Text.Encoding]::UTF8)
    try { $manifest = @($reader.ReadToEnd() -split "`r?`n" | Where-Object { $_ }) }
    finally { $reader.Dispose() }
} finally {
    $archive.Dispose()
}

foreach ($file in $required) {
    if ($entries -notcontains $file) { throw "Unsafe update archive: $file is missing from the zip" }
    if ($manifest -notcontains $file) { throw "Unsafe update archive: manifest.txt does not list $file" }
}

$unlisted = @($entries | Where-Object { $manifest -notcontains $_ })
if ($unlisted) { throw "Unsafe update archive: manifest.txt does not list $($unlisted -join ', ')" }
$phantom = @($manifest | Where-Object { $entries -notcontains $_ })
if ($phantom) { throw "Unsafe update archive: manifest.txt lists files the zip does not have: $($phantom -join ', ')" }

Write-Host "Update archive OK: $($entries.Count) files, manifest matches"
