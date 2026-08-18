# Builds the release artifacts for Portal Launcher:
#
#   PortalLauncher-Windows-MSVC-<version>.zip   <- what the in-app updater installs
#   PortalLauncher-Setup-<version>.exe          <- installer for new users
#   SHA256SUMS.txt
#
# The installer name deliberately leaves out "Windows-MSVC": the updater only
# accepts assets whose name contains the build artifact, so naming it this way
# keeps automatic updates flowing through the zip and never the installer.
#
# Signing: set PORTAL_SIGN_PFX (+ PORTAL_SIGN_PASSWORD) to sign both artifacts,
# or PORTAL_SIGN_COMMAND to a script that takes a file path and signs it in
# place (that is the escape hatch for cloud signing services). Unsigned builds
# still work; Windows just shows a SmartScreen warning on first run.

param(
    [string]$Version,
    [switch]$SkipBuild,
    [switch]$SkipInstaller
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo "build"
$installDir = Join-Path $repo "install"
$outDir = Join-Path $repo "dist"
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

function Get-LauncherVersion {
    $cmake = Get-Content (Join-Path $repo "CMakeLists.txt") -Raw
    $major = [regex]::Match($cmake, 'set\(Launcher_VERSION_MAJOR\s+(\d+)\)').Groups[1].Value
    $minor = [regex]::Match($cmake, 'set\(Launcher_VERSION_MINOR\s+(\d+)\)').Groups[1].Value
    $patch = [regex]::Match($cmake, 'set\(Launcher_VERSION_PATCH\s+(\d+)\)').Groups[1].Value
    if (-not $major) { throw "Could not read Launcher_VERSION_* from CMakeLists.txt" }
    return "$major.$minor.$patch"
}

function Invoke-Signing([string]$file) {
    if ($env:PORTAL_SIGN_COMMAND) {
        Write-Host "  signing $([IO.Path]::GetFileName($file)) via PORTAL_SIGN_COMMAND"
        & $env:PORTAL_SIGN_COMMAND $file
        if ($LASTEXITCODE -ne 0) { throw "Signing command failed for $file" }
        return $true
    }
    if ($env:PORTAL_SIGN_PFX) {
        $signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" -EA SilentlyContinue |
            Sort-Object FullName -Descending | Select-Object -First 1
        if (-not $signtool) { throw "PORTAL_SIGN_PFX is set but signtool.exe was not found (install the Windows SDK)" }
        Write-Host "  signing $([IO.Path]::GetFileName($file)) with $($env:PORTAL_SIGN_PFX)"
        $args = @("sign", "/fd", "SHA256", "/tr", "http://timestamp.digicert.com", "/td", "SHA256", "/f", $env:PORTAL_SIGN_PFX)
        if ($env:PORTAL_SIGN_PASSWORD) { $args += @("/p", $env:PORTAL_SIGN_PASSWORD) }
        $args += $file
        & $signtool.FullName @args | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "signtool failed for $file" }
        return $true
    }
    return $false
}

if (-not $Version) { $Version = Get-LauncherVersion }
Write-Host "=== Packaging Portal Launcher $Version ===" -ForegroundColor Cyan

# 1. the bundled resource pack optimizer
& (Join-Path $PSScriptRoot "fetch-packsquash.ps1")

# 2. build
if (-not $SkipBuild) {
    Write-Host "Building..."
    & cmd /c "`"$vcvars`" >nul && cmake --build `"$buildDir`" --config Release"
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}

# 3. stage into install/
Write-Host "Staging install tree..."
& cmd /c "`"$vcvars`" >nul && cmake --install `"$buildDir`" --config Release" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed" }
if (-not (Test-Path (Join-Path $installDir "packsquash.exe"))) {
    Write-Warning "packsquash.exe is missing from the install tree - pack optimization will be unavailable"
}
# portable.txt must never ship: it moves the data dir into the install folder
Remove-Item (Join-Path $installDir "portable.txt") -Force -EA SilentlyContinue

# The archive updater uses this manifest for both halves of its transaction:
# first to back up/remove the old install, then to copy every extracted file
# into place. Without it, older updaters guess from the partially emptied
# destination and can leave the launcher executable missing.
$manifestPath = Join-Path $installDir "manifest.txt"
$manifestEntries = @(
    Get-ChildItem $installDir -Recurse -File |
        Where-Object { $_.FullName -ne $manifestPath } |
        ForEach-Object { $_.FullName.Substring($installDir.Length).TrimStart('\', '/').Replace('\', '/') }
)
$manifestEntries += "manifest.txt"
$manifestEntries = $manifestEntries | Sort-Object -Unique
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText($manifestPath, (($manifestEntries -join "`n") + "`n"), $utf8NoBom)

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Get-ChildItem $outDir -File | Remove-Item -Force -EA SilentlyContinue

# 4. zip (this is what the updater downloads)
$zipName = "PortalLauncher-Windows-MSVC-$Version.zip"
$zipPath = Join-Path $outDir $zipName
Write-Host "Creating $zipName..."
Compress-Archive -Path (Join-Path $installDir "*") -DestinationPath $zipPath -CompressionLevel Optimal -Force

# Fail packaging before an unsafe archive can ever reach GitHub.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $archiveNames = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/').TrimStart('/') })
    $requiredFiles = @("manifest.txt", "prismlauncher.exe", "prismlauncher_updater.exe", "Qt6Core.dll", "platforms/qwindows.dll")
    foreach ($required in $requiredFiles) {
        if ($archiveNames -notcontains $required) { throw "Unsafe update archive: $required is missing from $zipName" }
    }

    $manifestEntry = $archive.Entries | Where-Object { $_.FullName.Replace('\', '/').TrimStart('/') -eq "manifest.txt" } | Select-Object -First 1
    $reader = New-Object IO.StreamReader($manifestEntry.Open(), [Text.Encoding]::UTF8)
    try { $archivedManifest = @($reader.ReadToEnd() -split "`r?`n" | Where-Object { $_ }) }
    finally { $reader.Dispose() }
    foreach ($required in $requiredFiles) {
        if ($archivedManifest -notcontains $required) { throw "Unsafe update archive: manifest.txt does not list $required" }
    }
} finally {
    $archive.Dispose()
}

# 5. installer
$setupPath = Join-Path $outDir "PortalLauncher-Setup-$Version.exe"
if (-not $SkipInstaller) {
    $makensis = (Get-Command makensis -EA SilentlyContinue).Source
    if (-not $makensis) { $makensis = "C:\Program Files (x86)\NSIS\makensis.exe" }
    if (-not (Test-Path $makensis)) {
        Write-Warning "makensis not found - skipping the installer (winget install NSIS.NSIS)"
    } else {
        # optional plugin the installer uses for downloading; best effort
        $pluginRoot = Join-Path $repo "NSISPlugins"
        if (-not (Test-Path (Join-Path $pluginRoot "NScurl\Plugins"))) {
            try {
                New-Item -ItemType Directory -Force -Path $pluginRoot | Out-Null
                $nscurlZip = Join-Path $pluginRoot "NScurl.zip"
                Invoke-WebRequest -Uri "https://github.com/negrutiu/nsis-nscurl/releases/latest/download/NScurl.zip" `
                    -Headers @{ "User-Agent" = "PortalLauncher-Build" } -OutFile $nscurlZip
                Expand-Archive -Path $nscurlZip -DestinationPath (Join-Path $pluginRoot "NScurl") -Force
                Remove-Item $nscurlZip -Force
            } catch {
                Write-Warning "Could not fetch the NScurl plugin, building the installer without it: $($_.Exception.Message)"
            }
        }

        $nsi = Join-Path $buildDir "program_info\win_install.nsi"
        if (-not (Test-Path $nsi)) { throw "Generated installer script missing: $nsi (configure the build first)" }

        Write-Host "Building installer..."
        Push-Location $installDir
        try {
            & $makensis -NOCD $nsi | Select-Object -Last 3
            if ($LASTEXITCODE -ne 0) { throw "makensis failed" }
        } finally {
            Pop-Location
        }

        # the script writes <CommonName>-Setup.exe next to the install tree
        $produced = Join-Path $repo "PrismLauncher-Setup.exe"
        if (-not (Test-Path $produced)) { throw "Installer was not produced at $produced" }
        Move-Item $produced $setupPath -Force
    }
}

# 6. sign whatever we produced
$signed = $false
foreach ($artifact in @($setupPath, $zipPath)) {
    if ((Test-Path $artifact) -and $artifact.EndsWith(".exe")) {
        $signed = (Invoke-Signing $artifact) -or $signed
    }
}
if (-not $signed) {
    Write-Host "Not signed (no PORTAL_SIGN_PFX or PORTAL_SIGN_COMMAND set)." -ForegroundColor Yellow
}

# 7. checksums
# The updater downloads "<asset>.sha256" and hard-fails when it does not match,
# so the zip MUST ship one; content is the bare lowercase hex digest.
foreach ($artifact in (Get-ChildItem $outDir -File | Where-Object { $_.Extension -in ".zip", ".exe" })) {
    $digest = (Get-FileHash $artifact.FullName -Algorithm SHA256).Hash.ToLower()
    Set-Content -Path "$($artifact.FullName).sha256" -Value $digest -Encoding ascii -NoNewline
}

Write-Host "`n=== Artifacts in $outDir ===" -ForegroundColor Green
Get-ChildItem $outDir -File | ForEach-Object { "{0,10:N1} MB  {1}" -f ($_.Length / 1MB), $_.Name }
