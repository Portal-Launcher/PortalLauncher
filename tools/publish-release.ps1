# Publishes a GitHub release from the artifacts in dist/.
#
#   powershell -File tools/publish-release.ps1 -Version 1.0.2 -NotesFile notes.md
#
# Expects tools/package-release.ps1 to have run first. The GitHub token comes
# from the credential helper and is never printed or written to disk.

param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$NotesFile,
    [string]$Repo = "TinsleyDevers/PortalLauncher"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$distDir = Join-Path $repoRoot "dist"

if (-not (Test-Path $NotesFile)) { throw "Notes file not found: $NotesFile" }

$assets = Get-ChildItem $distDir -File | Where-Object { $_.Name -like "*$Version*" }
if (-not $assets) { throw "No artifacts for $Version in $distDir - run package-release.ps1 first" }
$zip = $assets | Where-Object { $_.Name -like "*Windows-MSVC-$Version.zip" }
if (-not $zip) { throw "The updater needs PortalLauncher-Windows-MSVC-$Version.zip and it is missing" }
if (-not ($assets | Where-Object { $_.Name -like "*Windows-MSVC-$Version.zip.sha256" })) {
    throw "Missing $($zip.Name).sha256 - the updater hard-fails without it"
}

# --- token, via the credential helper (stdin must come from a file on PS 5.1) ---
$inFile = [IO.Path]::GetTempFileName()
$outFile = [IO.Path]::GetTempFileName()
try {
    [IO.File]::WriteAllText($inFile, "protocol=https`nhost=github.com`n`n")
    Start-Process git -ArgumentList "credential", "fill" -RedirectStandardInput $inFile `
        -RedirectStandardOutput $outFile -NoNewWindow -Wait
    $token = (Get-Content $outFile | Where-Object { $_ -like "password=*" }) -replace "^password=", ""
} finally {
    Remove-Item $inFile, $outFile -Force -ErrorAction SilentlyContinue
}
if (-not $token) { throw "Could not get a GitHub token from the credential helper" }

$headers = @{
    Authorization = "token $token"
    "User-Agent"  = "PortalLauncher-Release"
    Accept        = "application/vnd.github+json"
}

# --- create the release (or reuse it if the tag was already published) ---
$existing = $null
try {
    $existing = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/tags/$Version" -Headers $headers
} catch {}

if ($existing) {
    Write-Host "Release $Version already exists, updating its notes and assets."
    $release = Invoke-RestMethod -Method Patch -Uri "https://api.github.com/repos/$Repo/releases/$($existing.id)" `
        -Headers $headers -ContentType "application/json" `
        -Body (@{ name = "Portal Launcher $Version"; body = [IO.File]::ReadAllText($NotesFile); draft = $false; prerelease = $false } | ConvertTo-Json)
} else {
    $release = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$Repo/releases" `
        -Headers $headers -ContentType "application/json" `
        -Body (@{
            tag_name = $Version
            name     = "Portal Launcher $Version"
            body     = [IO.File]::ReadAllText($NotesFile)   # never Get-Content -Raw here: PSPath notes break ConvertTo-Json
            draft    = $false
            prerelease = $false
            make_latest = "true"
        } | ConvertTo-Json)
}

# --- assets (replace any leftovers with the same name) ---
foreach ($asset in $assets) {
    $stale = $release.assets | Where-Object { $_.name -eq $asset.Name }
    foreach ($old in $stale) {
        Invoke-RestMethod -Method Delete -Uri "https://api.github.com/repos/$Repo/releases/assets/$($old.id)" -Headers $headers | Out-Null
    }
    Write-Host "Uploading $($asset.Name) ($([math]::Round($asset.Length/1MB,1)) MB)..."
    Invoke-RestMethod -Method Post -Headers $headers -ContentType "application/octet-stream" `
        -Uri "https://uploads.github.com/repos/$Repo/releases/$($release.id)/assets?name=$([uri]::EscapeDataString($asset.Name))" `
        -InFile $asset.FullName | Out-Null
}

Write-Host "`nPublished: $($release.html_url)" -ForegroundColor Green
