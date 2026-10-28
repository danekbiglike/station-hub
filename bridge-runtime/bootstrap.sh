#!/system/bin/sh
base=/data/local/stationhub-bridge
unset LD_PRELOAD
umask 077
lock="$base/supervisor.lock"
if ! mkdir "$lock" 2>/dev/null; then
  old=$(cat "$lock/pid" 2>/dev/null)
  if [ -n "$old" ] && kill -0 "$old" 2>/dev/null; then
    cmd=$(tr "\000" " " < "/proc/$old/cmdline" 2>/dev/null)
    case "$cmd" in *"$base/bootstrap.sh"*) exit 0;; esac
  fi
  rm -f "$lock/pid"; rmdir "$lock" 2>/dev/null || exit 0
  mkdir "$lock" 2>/dev/null || exit 0
fi
echo $$ > "$lock/pid"
# This separately supervised, loopback-only service does not change the voice proxy.
if [ -x "$base/rating-supervisor.sh" ]; then "$base/rating-supervisor.sh" >/dev/null 2>&1 & fi
bridge_pid=""
pilot_pid=""
cleanup_children() {
  if [ -n "$pilot_pid" ]; then kill -TERM "$pilot_pid" 2>/dev/null; wait "$pilot_pid" 2>/dev/null; pilot_pid=""; fi
  # Never tear down a proxy that may still be serving the speaker. The endpoint
  # change is asynchronous and a zero connection count alone cannot prove that
  # the speaker has adopted the original address.
  if [ -n "$bridge_pid" ]; then
    if kill -0 "$bridge_pid" 2>/dev/null; then
      echo bridge_left_running > "$base/status"
      : > "$base/disabled"
    fi
    bridge_pid=""
  fi
}
cleanup() { cleanup_children; rm -f "$lock/pid"; rmdir "$lock" 2>/dev/null; }
trap 'cleanup; exit 0' TERM INT
trap cleanup EXIT
while [ ! -e "$base/disabled" ]; do
  boot=$(getprop sys.boot_completed)
  dns=$(getprop net.dns1)
  key=$(sed -n 's/.*<string name="pairing_key">\([^<]*\)<.*/\1/p' /data/data/ru.stationhub.app/shared_prefs/station_hub.xml 2>/dev/null)
  if [ "$boot" != 1 ] || [ -z "$dns" ] || [ ${#key} -ne 16 ]; then sleep 5; continue; fi
  if ! "$base/voice-pilot" -mode check-hub >/dev/null 2>&1; then
    am startservice -n ru.stationhub.app/.AudioReceiverService >/dev/null 2>&1
    sleep 5
    "$base/voice-pilot" -mode check-hub >/dev/null 2>&1 || continue
  fi
  SSL_CERT_DIR=/system/etc/security/cacerts STATION_HUB_KEY="$key" STATION_HUB_DNS="$dns" GOMEMLIMIT=48MiB GOGC=50 "$base/bridge" -config "$base/config.json" > /dev/null 2>&1 &
  bridge_pid=$!
  ok=0
  for i in 1 2 3 4 5 6 7 8 9 10; do
    if /system/bin/curl -fsS --max-time 2 http://127.0.0.1:18443/healthz >/dev/null 2>&1; then ok=1; break; fi
    kill -0 "$bridge_pid" 2>/dev/null || break
    sleep 1
  done
  if [ "$ok" = 1 ] && kill -0 "$bridge_pid" 2>/dev/null; then
    "$base/voice-pilot" -mode pilot -bridge-pid "$bridge_pid" -timeout 0 > /dev/null 2>&1 &
    pilot_pid=$!
    misses=0
    hubcheck=0
    while [ ! -e "$base/disabled" ] && kill -0 "$bridge_pid" 2>/dev/null && kill -0 "$pilot_pid" 2>/dev/null; do
      if /system/bin/curl -fsS --max-time 2 http://127.0.0.1:18443/healthz >/dev/null 2>&1; then misses=0; else misses=$((misses+1)); fi
      [ "$misses" -lt 3 ] || break
      if [ "$hubcheck" -eq 0 ]; then
        if ! "$base/voice-pilot" -mode check-hub >/dev/null 2>&1; then
          am startservice -n ru.stationhub.app/.AudioReceiverService >/dev/null 2>&1
        fi
      fi
      hubcheck=$(((hubcheck+1)%6))
      sleep 5
    done
  fi
  cleanup_children
  [ -e "$base/disabled" ] || sleep 15
done
