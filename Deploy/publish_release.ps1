<#
.SYNOPSIS
    Ephemeris -- publishes a built release on GitHub: the tag v<version>, the assets with their checksums, and the text
    from docs\RELEASE_NOTES.md. The family's publish step (02.10.2026): the same script in all five instruments, but for
    the assets and the footer at its top.

.DESCRIPTION
    Run after Deploy\build_release.ps1 and the Quest APK (Quest\build_apk.ps1), with the build's changes committed. Needs the GitHub CLI
    (gh), logged in.

      1. the version from the project() line of CMakeLists.txt, the one source of truth
      2. the checks: docs\RELEASE_NOTES.md has a section "## <version>", README.md links no other version's downloads,
         every asset is there, the working tree is clean
      3. the assets under their published names in work\publish\v<version>, SHA256SUMS.txt of them, and the release
         text: the notes' section, the downloads, the footer
      4. git push, the tag v<version> (lightweight, as every release of the family), gh release create --verify-tag

    -DryRun stops after step 3 (nothing leaves the machine; the text is work\publish\v<version>\release.md). -Draft
    publishes the release as a draft.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Deploy\publish_release.ps1 -DryRun
    powershell -ExecutionPolicy Bypass -File Deploy\publish_release.ps1
#>
param([switch]$DryRun, [switch]$Draft)
$ErrorActionPreference = "Stop"
$Name = "Ephemeris"
$Repo = "reneweller-coding/Ephemeris"
# The assets: where the build leaves each file (relative to the repository; {v} is the version), the name it is
# published under, and what the release text says about it.
$Assets = @(
    @{ From = "dist\Ephemeris-{v}-Setup.exe"; As = "Ephemeris-{v}-Setup.exe";
       Says = "installs the standalone, the VST3, the renderer and the manual; the runtime is linked in" },
    @{ From = "dist\Ephemeris-{v}-portable.zip"; As = "Ephemeris-{v}-portable.zip";
       Says = "the same in one archive, nothing to install" },
    @{ From = "bin\quest\EphemerisQuest.apk"; As = "EphemerisQuest-{v}.apk";
       Says = "the native Meta Quest app (``adb install -r EphemerisQuest-{v}.apk``, developer mode)" },
    @{ From = "docs\manual\Ephemeris-Manual.pdf"; As = "Ephemeris-Manual.pdf";
       Says = "every page of the panel as a picture, every parameter" }
)
# Below the downloads.
$Footer = @'
Requirements: Windows 10 or 11 (x64) with AVX2 (every x86-64 since 2013), a VST3 host for the plugin, Meta Quest 2 or
later for the app. The installer is not code-signed: Windows' SmartScreen may warn once ("More info", "Run anyway").
Licence: AGPL-3.0.
The macOS zip (Apple Silicon) is built on GitHub's runners and attached here by the workflow `macos` within
the hour; it is signed ad hoc, not notarized, and not yet tried on a real Mac.
'@

$root = Split-Path -Parent $PSScriptRoot
$project = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($project -notmatch ('project\(\s*' + $Name + '\s+VERSION\s+([0-9.]+)')) { throw "no version in CMakeLists.txt" }
$Version = $Matches[1]
$tag = "v$Version"
Write-Host "$Name $Version"

# 2. The checks.
$notes = @(Get-Content -Encoding UTF8 (Join-Path $root "docs\RELEASE_NOTES.md"))
$start = -1
for ($i = 0; $i -lt $notes.Count; ++$i) {
    if ($notes[$i] -match ('^## ' + [regex]::Escape($Version) + '(\s|:|$)')) { $start = $i; break }
}
if ($start -lt 0) { throw "docs\RELEASE_NOTES.md has no section '## $Version'" }
$end = $notes.Count
for ($i = $start + 1; $i -lt $notes.Count; ++$i) { if ($notes[$i] -match '^## ') { $end = $i; break } }
$section = ""
if ($end -gt $start + 1) { $section = ($notes[($start + 1)..($end - 1)] -join "`n").Trim() }
if ($section -eq "") { throw "the section '## $Version' of docs\RELEASE_NOTES.md is empty" }
$readme = Get-Content -Encoding UTF8 (Join-Path $root "README.md") -Raw
$stale = @([regex]::Matches($readme, 'releases/download/v([0-9.]+)/') | ForEach-Object { $_.Groups[1].Value } |
           Where-Object { $_ -ne $Version } | Select-Object -Unique)
if ($stale.Count -gt 0) { throw "README.md still links the downloads of v$($stale -join ', v')" }
Push-Location $root
try {
    $dirty = @(& git status --porcelain)
    if ($dirty.Count -gt 0) { throw "the working tree is not clean (commit the build's changes first):`n$($dirty -join "`n")" }
} finally { Pop-Location }

# 3. The assets under their names, the checksums, the text.
$pub = Join-Path $root "work\publish\$tag"
if (Test-Path $pub) { Remove-Item $pub -Recurse -Force }
New-Item -ItemType Directory -Force $pub | Out-Null
$files = @()
$lines = @()
foreach ($a in $Assets) {
    $from = Join-Path $root ($a.From -replace '\{v\}', $Version)
    $as = $a.As -replace '\{v\}', $Version
    if (-not (Test-Path $from)) { throw "missing: $from" }
    $dst = Join-Path $pub $as
    Copy-Item $from $dst -Force
    $files += $dst
    $lines += ("- **{0}** -- {1}." -f $as, ($a.Says -replace '\{v\}', $Version))
}
$sums = Join-Path $pub "SHA256SUMS.txt"
$files | ForEach-Object { "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 $_).Hash.ToLower(), (Split-Path -Leaf $_) } |
    Set-Content -Encoding ascii $sums
$files += $sums
$lines += "- **SHA256SUMS.txt** -- the checksums of all of them."
$text = $section + "`n`n## Downloads`n`n" + ($lines -join "`n") + "`n`n" + $Footer.Trim() + "`n"
$textFile = Join-Path $pub "release.md"
[System.IO.File]::WriteAllText($textFile, $text, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "wrote $pub ($($files.Count) files)"
if ($DryRun) { Write-Host "dry run: nothing pushed, nothing published"; return }

# 4. Push, tag, release.
Push-Location $root
try {
    & git push origin master
    if ($LASTEXITCODE -ne 0) { throw "git push failed" }
    & git rev-parse -q --verify "refs/tags/$tag" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        & git tag $tag
        if ($LASTEXITCODE -ne 0) { throw "git tag failed" }
    }
    & git push origin $tag
    if ($LASTEXITCODE -ne 0) { throw "pushing the tag failed" }
    $ghArgs = @("release", "create", $tag) + $files + @("--repo", $Repo, "--title", "$Name $Version", "--notes-file", $textFile, "--verify-tag")
    if ($Draft) { $ghArgs += "--draft" }
    & gh @ghArgs
    if ($LASTEXITCODE -ne 0) { throw "gh release create failed" }
} finally { Pop-Location }
Write-Host "published $($Name) $($Version): https://github.com/$Repo/releases/tag/$tag"
