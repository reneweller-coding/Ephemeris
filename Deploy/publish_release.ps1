<#
.SYNOPSIS
    Ephemeris -- publishes a built release on GitHub: tag v<version>, the setup, the portable zip, the Quest APK, the
    manual and the checksums of them all, with docs\RELEASE_NOTES.md as its text.

.DESCRIPTION
    Run after Deploy\build_release.ps1 and Quest\build_apk.ps1. Needs the GitHub CLI (gh), logged in. The version comes
    from the project() line of CMakeLists.txt. The update check of the program (UpdateCheck.h) asks for exactly this
    release -- once the repository is public.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Deploy\publish_release.ps1
    powershell -ExecutionPolicy Bypass -File Deploy\publish_release.ps1 -Draft
#>
param([switch]$Draft)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$project = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($project -notmatch 'project\(\s*Ephemeris\s+VERSION\s+([0-9.]+)') { throw "no version in CMakeLists.txt" }
$Version = $Matches[1]
$files = @(
    (Join-Path $root "Deploy\out\Ephemeris-$Version-Setup.exe"),
    (Join-Path $root "Deploy\out\Ephemeris-$Version-portable.zip"),
    (Join-Path $root "build-quest\EphemerisQuest.apk")
)
foreach ($f in $files) { if (-not (Test-Path $f)) { throw "missing: $f" } }
$apk = Join-Path $env:TEMP "EphemerisQuest-$Version.apk"
Copy-Item $files[2] $apk -Force
$manual = Join-Path $root "docs\manual\Ephemeris-Manual.pdf"
if (-not (Test-Path $manual)) { throw "missing: $manual" }
$assets = @($files[0], $files[1], $apk, $manual)
# The checksums of what is published, as Noctuary's releases carry them.
$sums = Join-Path $env:TEMP "SHA256SUMS.txt"
$assets | ForEach-Object { "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 $_).Hash.ToLower(), (Split-Path -Leaf $_) } |
    Set-Content -Encoding ascii $sums
$assets += $sums
$notes = Join-Path $root "docs\RELEASE_NOTES.md"
$ghArgs = @("release", "create", "v$Version") + $assets + @("--title", "Ephemeris $Version", "--notes-file", $notes)
if ($Draft) { $ghArgs += "--draft" }
& gh @ghArgs
if ($LASTEXITCODE -ne 0) { throw "gh release create failed" }
Write-Host "published Ephemeris $Version"
