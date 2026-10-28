$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
$workspace = Split-Path -Parent $project

# Standard SDK environment variables take precedence; the original workspace
# tools directory is retained as a convenient local development fallback.
$jdk = $env:JAVA_HOME
if (-not $jdk) {
    $bundled = Join-Path $workspace 'tools\jdk17'
    if (Test-Path $bundled) { $jdk = Get-ChildItem -LiteralPath $bundled -Directory | Select-Object -First 1 -ExpandProperty FullName }
}
if (-not $jdk) {
    $javac = Get-Command javac.exe -ErrorAction SilentlyContinue
    if ($javac) { $jdk = Split-Path -Parent (Split-Path -Parent $javac.Source) }
}
if (-not $jdk -or -not (Test-Path (Join-Path $jdk 'bin\javac.exe'))) { throw 'JDK 17 required: set JAVA_HOME.' }
$sdk = if ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } elseif ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $workspace 'tools\android-sdk' }
$buildTools = Join-Path $sdk 'build-tools\35.0.0'
$androidJar = Join-Path $sdk 'platforms\android-35\android.jar'
if (-not (Test-Path $androidJar) -or -not (Test-Path (Join-Path $buildTools 'apksigner.bat'))) { throw 'Install Android SDK platform 35 and build-tools 35.0.0, then set ANDROID_HOME.' }
$build = Join-Path $project 'build'
$classes = Join-Path $build 'classes'
$dex = Join-Path $build 'dex'
New-Item -ItemType Directory -Path $classes, $dex -Force | Out-Null

$sources = @(Get-ChildItem -Path (Join-Path $project 'android\src') -Filter '*.java' -Recurse | ForEach-Object { $_.FullName })
& (Join-Path $jdk 'bin\javac.exe') --release 8 -encoding UTF-8 -classpath $androidJar -d $classes $sources
if ($LASTEXITCODE -ne 0) { throw 'Java compilation failed' }
& (Join-Path $jdk 'bin\jar.exe') cf (Join-Path $build 'classes.jar') -C $classes .
if ($LASTEXITCODE -ne 0) { throw 'Class archive failed' }
$env:JAVA_HOME = $jdk
& (Join-Path $buildTools 'd8.bat') --min-api 23 --lib $androidJar --output $dex (Join-Path $build 'classes.jar')
if ($LASTEXITCODE -ne 0) { throw 'Android bytecode compilation failed' }
$unsigned = Join-Path $build 'station-hub-unsigned.apk'
$aligned = Join-Path $build 'station-hub-aligned.apk'
$signed = Join-Path $build 'station-hub-aligned-debugSigned.apk'
& (Join-Path $buildTools 'aapt.exe') package -f -M (Join-Path $project 'android\AndroidManifest.xml') -I $androidJar -F $unsigned
if ($LASTEXITCODE -ne 0) { throw 'Android package creation failed' }
Push-Location $dex
try { & (Join-Path $jdk 'bin\jar.exe') uf $unsigned classes.dex; if ($LASTEXITCODE -ne 0) { throw 'Could not add bytecode to APK' } } finally { Pop-Location }
& (Join-Path $buildTools 'zipalign.exe') -f 4 $unsigned $aligned
if ($LASTEXITCODE -ne 0) { throw 'APK alignment failed' }
$keystore = Join-Path $env:USERPROFILE '.android\debug.keystore'
if (-not (Test-Path $keystore)) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $keystore) | Out-Null
    & (Join-Path $jdk 'bin\keytool.exe') -genkeypair -keystore $keystore -storepass android -alias androiddebugkey -keypass android -dname 'CN=Android Debug,O=Android,C=US' -keyalg RSA -keysize 2048 -validity 10000
    if ($LASTEXITCODE -ne 0) { throw 'Could not create Android debug keystore' }
}
& (Join-Path $buildTools 'apksigner.bat') sign --ks $keystore --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out $signed $aligned
if ($LASTEXITCODE -ne 0) { throw 'APK signing failed' }
& (Join-Path $buildTools 'apksigner.bat') verify $signed
if ($LASTEXITCODE -ne 0) { throw 'APK signature verification failed' }
Write-Host "Built $signed"
