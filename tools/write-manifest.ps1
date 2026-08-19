# Writes the manifest.txt that ships inside a Windows build.
#
# The updater installs exactly what this file lists, and prunes files an older
# version shipped that the new one does not. Without it the updater has to guess
# at the file list, which is how builds up to 1.0.3 could leave an install with
# no launcher executable in it. Entries are relative, forward-slashed paths,
# UTF-8 without a BOM, and the manifest lists itself.

param(
    [Parameter(Mandatory = $true)][string]$Root
)

$ErrorActionPreference = "Stop"

$rootPath = (Resolve-Path $Root).Path.TrimEnd('\', '/')
$manifestPath = Join-Path $rootPath "manifest.txt"

$entries = @(
    Get-ChildItem $rootPath -Recurse -File |
        Where-Object { $_.FullName -ne $manifestPath } |
        ForEach-Object { $_.FullName.Substring($rootPath.Length).TrimStart('\', '/').Replace('\', '/') }
)
$entries += "manifest.txt"
$entries = $entries | Sort-Object -Unique

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText($manifestPath, (($entries -join "`n") + "`n"), $utf8NoBom)

Write-Host "Wrote $manifestPath ($($entries.Count) entries)"
