<#
.SYNOPSIS
    Ephemeris -- builds a release: binaries, tests, manual, staging, checksums, portable zip, setup.

.DESCRIPTION
    After Phosphene's Deploy\build_release.ps1, without its data download (Ephemeris has no data files).

      1. configure and build build\release (CMakePresets.json's release preset: icx, static runtime, AVX2) where oneAPI is
         installed (26.09.2026: the whole engine 10 to 15 % faster than MSVC's, pluginval passes; -Compiler msvc
         builds as before)
      2. run the tests (ctest), unless -SkipTests; pluginval at strictness 10 where it is unpacked
      3. the manual: Tools\manual\make_manual.py with the release's eph_render
      4. stage what is installed under dist\stage, and nothing else
      5. check the stage: every file there, the binaries without a DLL dependency on the MSVC runtime
      6. SHA256SUMS.txt, the portable zip, and the setup with Inno Setup (ISCC), unless -NoSetup

    Signing: with EPH_SIGN_THUMBPRINT (a certificate in the user's store) or EPH_SIGN_PFX and EPH_SIGN_PASSWORD (a .pfx
    file), the binaries and the setup are signed with signtool (SHA-256, timestamped: EPH_SIGN_TIMESTAMP, else
    DigiCert's server); without them the release is built unsigned (Windows' SmartScreen then warns once).

    The version comes from the project() line of CMakeLists.txt, the one source of truth.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Deploy\build_release.ps1
    powershell -ExecutionPolicy Bypass -File Deploy\build_release.ps1 -NoSetup -SkipTests
#>
param(
    [switch]$NoSetup,
    [switch]$SkipTests,
    [string]$Generator = "Visual Studio 18 2026",
    [ValidateSet("", "icx", "msvc")][string]$Compiler = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$deploy = Join-Path $root "Deploy"
$build = Join-Path $root "build\release-msvc"   # -Compiler msvc; icx: build\release

$project = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($project -notmatch 'project\(\s*Ephemeris\s+VERSION\s+([0-9.]+)') { throw "no version in CMakeLists.txt" }
$Version = $Matches[1]
Write-Host "Ephemeris $Version"

# 1. Build: with icx where oneAPI is installed (its environment and Visual Studio's, Ninja from Visual Studio), else MSVC.
$icxVars = Get-ChildItem "C:\Program Files (x86)\Intel\oneAPI\compiler\*\env\vars.bat" -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -Last 1
if ($Compiler -eq "") { $Compiler = if ($icxVars) { "icx" } else { "msvc" } }
Write-Host "compiler: $Compiler"
if ($Compiler -eq "icx") {
    if (-not $icxVars) { throw "oneAPI's icx not found" }
    $build = Join-Path $root "build\release"
    $vcvars = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars64.bat" | Select-Object -First 1
    $ninja = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" | Select-Object -First 1
    $installer = "C:\Program Files (x86)\Microsoft Visual Studio\Installer"
    $script = Join-Path $env:TEMP "ephemeris_icx_build.cmd"
    @(
        "@echo off",
        "call `"$($vcvars.FullName)`" > nul",
        "set PATH=$installer;%PATH%",
        "call `"$($icxVars.FullName)`" intel64 > nul",
        "set PATH=$($ninja.DirectoryName);%PATH%",
        "cd /d `"$root`"",
        "cmake --preset release || exit /b 1",
        "cmake --build --preset release --parallel 8 || exit /b 1"
    ) | Set-Content -Encoding ascii $script
    & cmd /c $script
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
    $render = Join-Path $build "Tools\render\eph_render.exe"
} else {
    & cmake -S $root -B $build -G $Generator -A x64 -DEPH_STATIC_RUNTIME=ON -DEPH_BUILD_PLUGIN=ON -DEPH_BUILD_TOOLS=ON
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }
    & cmake --build $build --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
    $render = Join-Path $build "Tools\render\Release\eph_render.exe"
}

# Signing where a certificate is given (see the description); a no-op without one.
function Sign-Files([string[]]$files) {
    $thumb = $env:EPH_SIGN_THUMBPRINT
    $pfx = $env:EPH_SIGN_PFX
    if (-not $thumb -and -not $pfx) { return }
    $signtool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -Last 1
    if (-not $signtool) { throw "signtool not found (Windows SDK)" }
    $stamp = if ($env:EPH_SIGN_TIMESTAMP) { $env:EPH_SIGN_TIMESTAMP } else { "http://timestamp.digicert.com" }
    $signArgs = @("sign", "/fd", "sha256", "/tr", $stamp, "/td", "sha256")
    if ($thumb) { $signArgs += @("/sha1", $thumb) } else { $signArgs += @("/f", $pfx, "/p", $env:EPH_SIGN_PASSWORD) }
    & $signtool.FullName @signArgs @files
    if ($LASTEXITCODE -ne 0) { throw "signing failed" }
    Write-Host "signed $($files.Count) files"
}

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
& python (Join-Path $root "Tools\manual\make_manual.py") --render $render
if ($LASTEXITCODE -ne 0) { throw "manual failed" }

# 4. Stage.
$stage = Join-Path $root "dist\stage"
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
        $deps = & $dumpbin.FullName /DEPENDENTS $b | Select-String -Pattern "(?i)((vcruntime|msvcp)\d+.*|libmmd|svml_disp\w*|libirngmd|libiomp\w*)\.dll"
        if ($deps) { throw "$b depends on a runtime DLL (MSVC's or Intel's): $deps" }
    }
    Write-Host "no dynamic MSVC or Intel runtime in $($binaries.Count) binaries"
    Sign-Files $binaries
} else {
    Write-Host "dumpbin not found: the runtime check is skipped"
}

# 6. Checksums, portable zip, setup.
$out = Join-Path $root "dist"
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
    Sign-Files @((Join-Path $out "Ephemeris-$Version-Setup.exe"))
    Write-Host "wrote $(Join-Path $out "Ephemeris-$Version-Setup.exe")"
}
