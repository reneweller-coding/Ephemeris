# Fetches what the Quest build needs into ThirdParty/ (ignored by git):
#   openxr-loader/  Khronos OpenXR loader for Android (prefab AAR from Maven Central: headers + libopenxr_loader.so)
#   oboe/           Google Oboe (low-latency Android audio), built from source by CMake
#
# Noctuary (the sibling project) already carries both. When its ThirdParty folder is there, this
# script makes directory junctions into it instead of downloading 130 MB again; -Copy copies and
# -Download ignores the sibling altogether.
param(
    [string]$Sibling = "G:\Tools\VRAudio\AmbientSynth\ThirdParty",
    [switch]$Copy,
    [switch]$Download
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$third = Join-Path $root "ThirdParty"
New-Item -ItemType Directory -Force $third | Out-Null

# Takes a folder from the sibling checkout: a junction by default, a copy with -Copy.
function Get-FromSibling([string]$name, [string]$marker) {
    if ($Download) { return $false }
    $src = Join-Path $Sibling $name
    if (-not (Test-Path (Join-Path $src $marker))) { return $false }
    $dst = Join-Path $third $name
    if (Test-Path (Join-Path $dst $marker)) { Write-Host "$name already here"; return $true }
    if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
    if ($Copy) {
        Copy-Item -Recurse $src $dst
        Write-Host "$name copied from $src"
    } else {
        & cmd /c mklink /J "`"$dst`"" "`"$src`"" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "cannot link $dst -> $src (use -Copy)" }
        Write-Host "$name linked to $src"
    }
    return $true
}

$loaderDir = Join-Path $third "openxr-loader"
if (-not (Get-FromSibling "openxr-loader" "prefab")) {
    if (-not (Test-Path (Join-Path $loaderDir "prefab"))) {
        $meta = Invoke-WebRequest -UseBasicParsing "https://repo1.maven.org/maven2/org/khronos/openxr/openxr_loader_for_android/maven-metadata.xml"
        $version = ([xml]$meta.Content).metadata.versioning.release
        $aar = Join-Path $third "openxr_loader_for_android.zip"
        Invoke-WebRequest -UseBasicParsing "https://repo1.maven.org/maven2/org/khronos/openxr/openxr_loader_for_android/$version/openxr_loader_for_android-$version.aar" -OutFile $aar
        New-Item -ItemType Directory -Force $loaderDir | Out-Null
        Expand-Archive -Path $aar -DestinationPath $loaderDir -Force
        Remove-Item $aar
        Write-Host "OpenXR loader $version"
    }
}

$oboe = Join-Path $third "oboe"
if (-not (Get-FromSibling "oboe" "CMakeLists.txt")) {
    if (-not (Test-Path (Join-Path $oboe "CMakeLists.txt"))) {
        & git clone --depth 1 https://github.com/google/oboe.git $oboe
        if ($LASTEXITCODE -ne 0) { throw "git clone oboe failed" }
    }
}

Write-Host "ThirdParty ready: $third"
