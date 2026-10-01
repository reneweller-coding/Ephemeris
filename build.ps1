<#
.SYNOPSIS
    Builds, tests and starts this instrument -- the same script in every repository of the family (01.10.2026).

.DESCRIPTION
    .\build.ps1 [msvc | icx | release | quest] [-Config Debug] [-Test] [-Run] [-Fresh] [-Jobs n]

      msvc     Visual Studio's compiler: build\msvc (the solution is there), the programs in bin\msvc
      icx      Intel's oneAPI compiler (Ninja, Release, static runtime): build\icx, the programs in bin\icx
      release  what ships (build\release); Deploy\build_release.ps1 makes the setup and the zip in dist\ from it
      quest    the Meta Quest app (Quest\build_apk.ps1): build\quest, the APK in bin\quest

    -Config Debug   the Debug configuration (msvc only): bin\msvc-Debug
    -Test           run the tests of that tree afterwards (ctest)
    -Run            start the standalone from bin\<preset> afterwards
    -Fresh          configure from scratch (cmake --fresh); nothing is deleted

    The trees and folders are cmake\Family.cmake's: build\<preset>, bin\<preset>, dist\, work\.

.EXAMPLE
    .\build.ps1                 # msvc, Release
    .\build.ps1 icx -Test -Run
#>
param(
    [Parameter(Position = 0)][ValidateSet("msvc", "icx", "release", "quest")][string]$Preset = "msvc",
    [ValidateSet("Release", "Debug")][string]$Config = "Release",
    [switch]$Test,
    [switch]$Run,
    [switch]$Fresh,
    # The machine is shared with other builds and with the user: at most 8 jobs.
    [int]$Jobs = 8
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
if ((Get-Content (Join-Path $root "CMakeLists.txt") -Raw) -notmatch 'project\(\s*(\w+)') { throw "no project() in CMakeLists.txt" }
$product = $Matches[1]

# A batch file's environment, read back into this PowerShell (Visual Studio's and oneAPI's setup are batch files).
function Import-CmdEnvironment([string]$batch, [string]$arguments = "") {
    $tmp = [System.IO.Path]::GetTempFileName()
    cmd /c " `"$batch`" $arguments > nul 2>&1 && set > `"$tmp`" "
    foreach ($line in Get-Content $tmp) {
        if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
    Remove-Item $tmp -Force
}

# icx needs Visual Studio's environment (its linker, the SDK), oneAPI's and Ninja from Visual Studio's CMake.
function Enter-IntelToolchain {
    if (Get-Command icx -ErrorAction SilentlyContinue) { return }
    $vcvars = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars64.bat" -ErrorAction SilentlyContinue |
              Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $vcvars) { throw "vcvars64.bat not found (Visual Studio's C++ workload)" }
    Import-CmdEnvironment $vcvars.FullName
    $vars = Get-ChildItem "C:\Program Files (x86)\Intel\oneAPI\compiler\*\env\vars.bat" -ErrorAction SilentlyContinue |
            Sort-Object FullName | Select-Object -Last 1
    if (-not $vars) { throw "oneAPI's icx not found (C:\Program Files (x86)\Intel\oneAPI\compiler)" }
    Import-CmdEnvironment $vars.FullName "intel64"
    $ninja = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" -ErrorAction SilentlyContinue |
             Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $ninja) { throw "ninja.exe not found (Visual Studio's CMake component)" }
    $env:PATH = "$($ninja.DirectoryName);$env:PATH"
    if (-not (Get-Command icx -ErrorAction SilentlyContinue)) { throw "icx is not on the PATH after oneAPI's vars.bat" }
}

if ($Preset -eq "quest") {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "Quest\build_apk.ps1")
    if ($LASTEXITCODE -ne 0) { throw "the Quest build failed" }
    Write-Host "APK: $(Join-Path $root "bin\quest")"
    return
}
$debugBuild = $Config -eq "Debug"
if ($debugBuild -and $Preset -ne "msvc") { throw "-Config Debug is for the msvc tree only (icx and release are Release trees)" }
if ($Preset -ne "msvc") { Enter-IntelToolchain }

Push-Location $root
try {
    $configure = @("--preset", $Preset)
    if ($Fresh) { $configure += "--fresh" }
    & cmake @configure
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }
    $buildPreset = if ($debugBuild) { "msvc-debug" } else { $Preset }
    & cmake --build --preset $buildPreset --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
    if ($Test) {
        & ctest --preset $buildPreset
        if ($LASTEXITCODE -ne 0) { throw "tests failed" }
    }
} finally {
    Pop-Location
}

$bin = Join-Path $root ("bin\" + $Preset + $(if ($debugBuild) { "-Debug" } else { "" }))
if ($Preset -ne "release") { Write-Host "programs: $bin" }
if ($Run) {
    $exe = Join-Path $bin "$product.exe"
    if ($Preset -eq "release") { $exe = Get-ChildItem -Recurse (Join-Path $root "build\release") -Filter "$product.exe" | Select-Object -First 1 -ExpandProperty FullName }
    if (-not (Test-Path $exe)) { throw "not found: $exe" }
    Start-Process $exe
}
