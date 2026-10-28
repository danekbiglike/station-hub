param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (Test-Path -LiteralPath $Destination) { throw 'Destination already exists; choose a new empty path.' }
$files = @(
  '.gitignore','.gitattributes','README.md','LICENSE','SUPPORT.md','CONTRIBUTING.md','SECURITY.md','AGENTS.md','build.ps1',
  '.github/FUNDING.yml','.github/ISSUE_TEMPLATE/bug_report.yml','docs/ARCHITECTURE.md','docs/DEVELOPMENT.md','docs/PROTOCOL.md','docs/ANNOUNCEMENT.md','docs/RELEASE_CHECKLIST.md','docs/ROADMAP.md',
  'scripts/export-public.ps1','android/AndroidManifest.xml',
  'android/src/ru/stationhub/app/AudioReceiverService.java','android/src/ru/stationhub/app/BootReceiver.java',
  'android/src/ru/stationhub/app/BridgeBootstrap.java','android/src/ru/stationhub/app/HubBridgeServer.java',
  'android/src/ru/stationhub/app/MainActivity.java','android/src/ru/stationhub/app/Pairing.java','android/src/ru/stationhub/app/PairingProvider.java',
  'android/src/ru/stationhub/app/StationControlServer.java','android/src/ru/stationhub/app/StationMediaMonitor.java',
  'android/src/ru/stationhub/app/StationRatingMonitor.java','android/src/ru/stationhub/app/StationVolumeMonitor.java',
  'android/src/ru/stationhub/app/VoiceRouter.java',
  'windows/station-hub.cmd','windows/start-station-audio.cmd','windows/station_audio.py',
  'windows/native/build.cmd','windows/native/test.cmd','windows/native/station_hub.cpp',
  'windows/native/station_widget.cpp','windows/native/station_widget_ui.inl','windows/native/widget_render.inl','windows/native/station_control.h',
  'windows/native/volume_intent.h','windows/native/widget_layout.h','windows/native/widget_art.h','windows/native/widget_pill.h','windows/native/yandex_like.cpp','windows/native/yandex_like.h',
  'windows/native/station_discovery.cpp','windows/native/station_discovery.h','windows/native/mdns_parser.h','windows/native/secure_pairing.cpp','windows/native/secure_pairing.h',
  'tests/test_protocol.py','tests/test_volume_intent.cpp','tests/test_widget_layout.cpp','tests/test_widget_art.cpp','tests/test_mdns.cpp','tests/discovery_probe.cpp','tests/test_pairing_provider.ps1','tests/test_secure_pairing.cpp','tests/fake_adb.cpp','tests/mock_station.go',
  'rating-service/main.go','rating-service/main_test.go',
  'bridge-runtime/main.go','bridge-runtime/main_test.go','bridge-runtime/bootstrap.sh','bridge-runtime/boot-start.sh',
  'bridge-runtime/install-hook.sh','bridge-runtime/rollback-hook.sh','bridge-runtime/rating-supervisor.sh',
  'bridge-patch/README.md','bridge-patch/config.example.json','bridge-patch/upstream.patch',
  'bridge-patch/stationhub.go','bridge-patch/stationhub_test.go','bridge-patch/LICENSE.upstream','bridge-patch/NOTICE.upstream'
)
# Export is deliberately an allowlist, never a recursive copy of working folders.
# Builds, logs, device dumps, credentials and downloaded dependencies stay local.
foreach ($name in $files) {
    $src = Join-Path $root $name
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { throw "Missing expected source file: $name" }
}
New-Item -ItemType Directory -Path $Destination | Out-Null
foreach ($name in $files) {
    $target = Join-Path $Destination $name
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $target
}
Write-Host "Exported $($files.Count) explicitly listed files to $Destination. Review docs/RELEASE_CHECKLIST.md before publication."
