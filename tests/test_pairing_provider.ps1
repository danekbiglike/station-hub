param([Parameter(Mandatory=$true)][string]$Serial,[switch]$AllowDevice)
$ErrorActionPreference='Stop'
if ($Serial -notmatch '^emulator-[0-9]+$' -and -not $AllowDevice) { throw 'Use an emulator, or explicitly specify -AllowDevice for a device you own.' }
$root=Split-Path -Parent $PSScriptRoot
$workspace=Split-Path -Parent $root
$adb=if($env:STATION_HUB_ADB){$env:STATION_HUB_ADB}else{Join-Path $workspace 'tools\platform-tools\adb.exe'}
$sdk=if($env:ANDROID_HOME){$env:ANDROID_HOME}else{Join-Path $workspace 'tools\android-sdk'}
$jdk=if($env:JAVA_HOME){$env:JAVA_HOME}else{Get-ChildItem (Join-Path $workspace 'tools\jdk17') -Directory | Select-Object -First 1 -ExpandProperty FullName}
$tools=Join-Path $sdk 'build-tools\35.0.0'
$jar=Join-Path $sdk 'platforms\android-35\android.jar'
$build=Join-Path $root 'build\provider-validation'
$provider='ru.stationhub.providercheck'
$client='ru.stationhub.clientcheck'
foreach($name in @($provider,$client)){
  $existing=(& $adb -s $Serial shell pm list packages $name | Out-String)
  if($existing -match [regex]::Escape("package:$name")){throw "Validation package $name already exists; refusing to overwrite it."}
}
New-Item -ItemType Directory -Force -Path $build | Out-Null
$providerManifest=@'
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="ru.stationhub.providercheck">
 <uses-sdk android:minSdkVersion="23" android:targetSdkVersion="28" />
 <application android:debuggable="true" android:allowBackup="false">
  <provider android:name="ru.stationhub.app.PairingProvider" android:authorities="ru.stationhub.providercheck.pairing" android:exported="true" android:readPermission="android.permission.DUMP" android:writePermission="android.permission.DUMP" android:grantUriPermissions="false" />
 </application>
</manifest>
'@
$clientManifest=@'
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="ru.stationhub.clientcheck">
 <uses-sdk android:minSdkVersion="23" android:targetSdkVersion="28" />
 <application android:debuggable="true" android:allowBackup="false" android:hasCode="false" />
</manifest>
'@
$installed=@()
try{
 foreach($item in @(@('provider',$providerManifest),@('client',$clientManifest))){
  $name=$item[0];$folder=Join-Path $build $name;New-Item -ItemType Directory -Force -Path $folder | Out-Null;$manifest=Join-Path $folder 'AndroidManifest.xml';[IO.File]::WriteAllText($manifest,$item[1],[Text.UTF8Encoding]::new($false))
  $unsigned=Join-Path $build "$name-unsigned.apk";$aligned=Join-Path $build "$name-aligned.apk";$signed=Join-Path $build "$name-signed.apk"
  & (Join-Path $tools 'aapt.exe') package -f -M $manifest -I $jar -F $unsigned
  if($LASTEXITCODE -ne 0){throw "Could not package $name test APK"}
  if($name -eq 'provider'){
   Push-Location (Join-Path $root 'build\dex')
   try {& (Join-Path $jdk 'bin\jar.exe') uf $unsigned classes.dex;if($LASTEXITCODE -ne 0){throw 'Could not add test bytecode'}}finally{Pop-Location}
  }
  & (Join-Path $tools 'zipalign.exe') -f 4 $unsigned $aligned
  if($LASTEXITCODE -ne 0){throw "Could not align $name test APK"}
  $env:JAVA_HOME=$jdk
  & (Join-Path $tools 'apksigner.bat') sign --ks (Join-Path $env:USERPROFILE '.android\debug.keystore') --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out $signed $aligned
  if($LASTEXITCODE -ne 0){throw "Could not sign $name test APK"}
  & $adb -s $Serial install $signed | Out-Null
  if($LASTEXITCODE -ne 0){throw "Could not install $name validation package"}
  $installed+=if($name -eq 'provider'){$provider}else{$client}
 }
 $uri='content://ru.stationhub.providercheck.pairing/key'
 $shellResponse=(& $adb -s $Serial shell content query --uri $uri --projection pairing_key 2>&1 | Out-String)
 if($shellResponse -notmatch 'pairing_key=[0-9a-f]{16}(\s|$)'){throw 'The authorized ADB shell could not read the validation key.'}
 $shellResponse=$null
 $oldPreference=$ErrorActionPreference;$ErrorActionPreference='Continue'
 try{$denied=(& $adb -s $Serial shell run-as $client content query --uri $uri --projection pairing_key 2>&1 | Out-String)}finally{$ErrorActionPreference=$oldPreference}
 if($denied -notmatch 'Permission Denial|SecurityException' -or $denied -notmatch 'providercheck|android.permission.DUMP'){throw 'The provider did not produce the expected permission denial for an unprivileged client.'}
 if($denied -match 'pairing_key=[0-9a-f]{16}'){throw 'The unprivileged client unexpectedly received a key.'}
 Write-Host 'ADB shell access passed; ordinary application access denied.'
}finally{
 foreach($name in $installed){& $adb -s $Serial uninstall $name | Out-Null}
}
