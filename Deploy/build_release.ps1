<#
.SYNOPSIS
    Ephemeris -- builds a release: binaries, tests, manual, staging, checksums, portable zip, setup.

.DESCRIPTION
    After Phosphene's Deploy\build_release.ps1, without its data download (Ephemeris has no data files).

      1. configure and build build-release (Release, static MSVC runtime, AVX2)
      2. run the tests (ctest), unless -SkipTests; pluginval at strictness 10 where it is unpacked
      3. the manual: Tools\manual\make_manual.py with the release's eph_render
      4. stage what is installed under Deploy\stage, and nothing else
      5. check the stage: every file there, the binaries without a DLL dependency on the MSVC runtime
      6. SHA256SUMS.txt, the portable zip, and the setup with Inno Setup (ISCC), unless -NoSetup

    The version comes from the project() line of CMakeLists.txt, the one source of truth.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Deploy\build_release.ps1
    powershell -ExecutionPolicy Bypass -File Deploy\build_release.ps1 -NoSetup -SkipTests
#>
param(
    [switch]$NoSetup,
    [switch]$SkipTests,
    [string]$Generator = "Visual Studio 18 2026"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$deploy = Join-Path $root "Deploy"
$build = Join-Path $root "build-release"

$project = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($project -notmatch 'project\(\s*Ephemeris\s+VERSION\s+([0-9.]+)') { throw "no version in CMakeLists.txt" }
$Version = $Matches[1]
Write-Host "Ephemeris $Version"

# 1. Build.
& cmake -S $root -B $build -G $Generator -A x64 -DEPH_STATIC_RUNTIME=ON -DEPH_BUILD_PLUGIN=ON -DEPH_BUILD_TOOLS=ON
if ($LASTEXITCODE -ne 0) { throw "configure failed" }
& cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# 2. Tests (they run muted where they may: EPH_MUTE; the host test lifts it for itself).
if (-not $SkipTests) {
    $env:EPH_MUTE = "1"
    & ctest --test-dir $build -C Release -j 6 --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "tests failed" }
}
# 2b. pluginval (Tracktion) at strictness 10 on the release VST3, where it is unpacked (ThirdParty\pluginval).
#     A GUI program: Start-Process waits for it, and its exit code is the verdict.
$pluginval = Join-Path $root "ThirdParty\pluginval\pluginval.exe"
if (Test-Path $pluginval) {
    $env:EPH_MUTE = "1"
    $vst3 = Join-Path $build "Plugin\Ephemeris_artefacts\Release\VST3\Ephemeris.vst3"
    $p = Start-Process -FilePath $pluginval -ArgumentList @("--strictness-level", "10", "--timeout-ms", "900000", "--validate", "`"$vst3`"") -Wait -PassThru -NoNewWindow
    if ($p.ExitCode -ne 0) { throw "pluginval failed ($($p.ExitCode))" }
    Write-Host "pluginval: strictness 10 passed"
} else {
    Write-Host "pluginval not in ThirdParty\pluginval: skipped"
}

# 3. The manual, from this build's eph_render (the screenshots in docs\screenshots are committed).
$render = Join-Path $build "Tools\render\Release\eph_render.exe"
& python (Join-Path $root "Tools\manual\make_manual.py") --render $render
if ($LASTEXITCODE -ne 0) { throw "manual failed" }

# 4. Stage.
$stage = Join-Path $deploy "stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
$art = Join-Path $build "Plugin\Ephemeris_artefacts\Release"
Copy-Item (Join-Path $art "Standalone\Ephemeris.exe") $stage
Copy-Item -Recurse (Join-Path $art "VST3\Ephemeris.vst3") $stage
Copy-Item $render $stage
Copy-Item (Join-Path $root "docs\manual\Ephemeris-Manual.pdf") $stage
Copy-Item (Join-Path $deploy "ephemeris.ico") $stage
Copy-Item (Join-Path $root "LICENSE") (Join-Path $stage "LICENSE.txt")
Copy-Item (Join-Path $root "README.md") (Join-Path $stage "README.txt")

# 5. Check: the files, and no dependency on the dynamic MSVC runtime.
$expected = @("Ephemeris.exe", "eph_render.exe", "Ephemeris.vst3", "Ephemeris-Manual.pdf", "ephemeris.ico", "LICENSE.txt", "README.txt")
foreach ($f in $expected) { if (-not (Test-Path (Join-Path $stage $f))) { throw "missing in the stage: $f" } }
$dumpbin = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($dumpbin) {
    $binaries = @((Join-Path $stage "Ephemeris.exe"), (Join-Path $stage "eph_render.exe")) +
                @(Get-ChildItem -Recurse (Join-Path $stage "Ephemeris.vst3") -Filter *.vst3 -File | ForEach-Object { $_.FullName })
    foreach ($b in $binaries) {
        $deps = & $dumpbin.FullName /DEPENDENTS $b | Select-String -Pattern "(?i)(vcruntime|msvcp)\d+.*\.dll"
        if ($deps) { throw "$b depends on the dynamic MSVC runtime: $deps" }
    }
    Write-Host "no dynamic MSVC runtime in $($binaries.Count) binaries"
} else {
    Write-Host "dumpbin not found: the runtime check is skipped"
}

# 6. Checksums, portable zip, setup.
$out = Join-Path $deploy "out"
New-Item -ItemType Directory -Force $out | Out-Null
$sums = Get-ChildItem -Recurse -File $stage | Sort-Object FullName | ForEach-Object {
    $rel = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
    "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower(), $rel
}
$sums | Set-Content -Encoding utf8 (Join-Path $stage "SHA256SUMS.txt")
$zip = Join-Path $out "Ephemeris-$Version-portable.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Write-Host "wrote $zip"
if (-not $NoSetup) {
    $iscc = Get-ChildItem "C:\Program Files\Inno Setup *\ISCC.exe", "C:\Program Files (x86)\Inno Setup *\ISCC.exe" -ErrorAction SilentlyContinue |
            Select-Object -First 1
    if (-not $iscc) { throw "Inno Setup not found. winget install JRSoftware.InnoSetup, or run with -NoSetup." }
    & $iscc.FullName "/DVersion=$Version" (Join-Path $deploy "Ephemeris.iss")
    if ($LASTEXITCODE -ne 0) { throw "setup failed" }
    Write-Host "wrote $(Join-Path $out "Ephemeris-$Version-Setup.exe")"
}
