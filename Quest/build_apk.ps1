# Builds the Ephemeris Quest APK without Gradle (after Phosphene's Quest\build_apk.ps1):
#   CMake/NDK -> libephquest.so, aapt2 link -> base.apk, add the native libs, zipalign, apksigner (debug key).
# Windows PowerShell 5.1.  powershell -File Quest\build_apk.ps1
param(
    [string]$Sdk = "C:\Android-Buildtools\sdk",
    [string]$NdkVersion = "27.2.12479018",
    [string]$BuildTools = "34.0.0",
    [string]$Platform = "android-34",
    [string]$Jdk = "C:\Android-Buildtools\jdk17",
    [string]$Config = "Release",
    # Parallel compile jobs. The machine is shared with other builds and the user (house rules: at most 8).
    [int]$Jobs = 8
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$quest = Join-Path $root "Quest"
$ndk = Join-Path $Sdk "ndk\$NdkVersion"
$bt = Join-Path $Sdk "build-tools\$BuildTools"
$androidJar = Join-Path $Sdk "platforms\$Platform\android.jar"
$build = Join-Path $root "build-quest"
# The version from the project() line of CMakeLists.txt; the version code 10000 major + 100 minor + patch.
if ((Get-Content (Join-Path $root "CMakeLists.txt") -Raw) -notmatch 'project\(\s*Ephemeris\s+VERSION\s+(\d+)\.(\d+)\.(\d+)') { throw "no version in CMakeLists.txt" }
$versionName = "$($Matches[1]).$($Matches[2]).$($Matches[3])"
$versionCode = [int]$Matches[1] * 10000 + [int]$Matches[2] * 100 + [int]$Matches[3]
$out = Join-Path $build "apk"

if (-not (Test-Path (Join-Path $root "ThirdParty\openxr-loader\prefab"))) { throw "ThirdParty missing: run Quest\fetch_thirdparty.ps1 first" }
$env:JAVA_HOME = $Jdk                                  # apksigner.bat looks for java here
$env:Path = (Join-Path $Jdk "bin") + ";" + $env:Path

# 1. the native library. No ninja is installed with this SDK, so the NDK's own make is used.
& cmake -S $quest -B $build -G "Unix Makefiles" `
    -DCMAKE_MAKE_PROGRAM="$ndk\prebuilt\windows-x86_64\bin\make.exe" `
    -DCMAKE_TOOLCHAIN_FILE="$ndk\build\cmake\android.toolchain.cmake" `
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=$Config
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
& cmake --build $build -j $Jobs
if ($LASTEXITCODE -ne 0) { throw "native build failed" }

# 2. staging: the native libraries
$libDir = Join-Path $out "lib\arm64-v8a"
New-Item -ItemType Directory -Force $libDir | Out-Null
Copy-Item (Join-Path $build "libephquest.so") $libDir -Force
Copy-Item (Join-Path $root "ThirdParty\openxr-loader\prefab\modules\openxr_loader\libs\android.arm64-v8a\libopenxr_loader.so") $libDir -Force

# 3. no assets: Ephemeris opens no data files, every sound is synthesised.

# 4. resources (the launcher icon at five densities) -> compiled, then the manifest -> base.apk (no code)
$resZip = Join-Path $out "res.zip"
if (Test-Path $resZip) { Remove-Item $resZip -Force }
& (Join-Path $bt "aapt2.exe") compile --dir (Join-Path $quest "res") -o $resZip
if ($LASTEXITCODE -ne 0) { throw "aapt2 compile failed" }
$base = Join-Path $out "base.apk"
if (Test-Path $base) { Remove-Item $base -Force }
& (Join-Path $bt "aapt2.exe") link -o $base --manifest (Join-Path $quest "AndroidManifest.xml") -R $resZip -I $androidJar --min-sdk-version 29 --target-sdk-version 32 --version-code $versionCode --version-name $versionName --replace-version
if ($LASTEXITCODE -ne 0) { throw "aapt2 link failed" }

# 5. add the libraries (jar keeps the zip valid; extractNativeLibs=true allows compressed .so)
Push-Location $out
& (Join-Path $Jdk "bin\jar.exe") uf $base lib\arm64-v8a\libephquest.so lib\arm64-v8a\libopenxr_loader.so
Pop-Location
if ($LASTEXITCODE -ne 0) { throw "jar failed" }

# 6. align and sign with a debug key (created once)
$aligned = Join-Path $out "aligned.apk"
& (Join-Path $bt "zipalign.exe") -f 4 $base $aligned
if ($LASTEXITCODE -ne 0) { throw "zipalign failed" }
$keystore = Join-Path $root "ThirdParty\debug.keystore"
if (-not (Test-Path $keystore)) {
    & (Join-Path $Jdk "bin\keytool.exe") -genkeypair -keystore $keystore -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 `
        -storepass android -keypass android -dname "CN=Android Debug,O=Android,C=US"
    if ($LASTEXITCODE -ne 0) { throw "keytool failed" }
}
$final = Join-Path $build "EphemerisQuest.apk"
& (Join-Path $bt "apksigner.bat") sign --ks $keystore --ks-pass pass:android --key-pass pass:android --out $final $aligned
if ($LASTEXITCODE -ne 0) { throw "apksigner failed" }

Write-Host ("APK size: {0:N0} bytes" -f (Get-Item $final).Length)
Write-Host "APK: $final"
Write-Host "install:  adb install -r `"$final`""
Write-Host "config:   adb push eph.cfg /sdcard/Android/data/com.reneweller.ephemeris.quest/files/eph.cfg"
