#!/system/bin/sh
base=/data/local/stationhub-bridge
lock="$base/rating-supervisor.lock"
umask 077
if ! mkdir "$lock" 2>/dev/null; then
  old=$(cat "$lock/pid" 2>/dev/null)
  if [ -n "$old" ] && kill -0 "$old" 2>/dev/null; then
    cmd=$(tr "\000" " " < "/proc/$old/cmdline" 2>/dev/null)
    case "$cmd" in *"$base/rating-supervisor.sh"*) exit 0;; esac
  fi
  rm -f "$lock/pid"; rmdir "$lock" 2>/dev/null || exit 0
  mkdir "$lock" 2>/dev/null || exit 0
fi
echo $$ > "$lock/pid"
child=""
cleanup() { if [ -n "$child" ]; then kill "$child" 2>/dev/null; fi; rm -f "$lock/pid"; rmdir "$lock" 2>/dev/null; }
trap 'cleanup; exit 0' TERM INT
trap cleanup EXIT
while :; do
  dns=$(getprop net.dns1)
  if [ "$(getprop sys.boot_completed)" != 1 ] || [ -z "$dns" ] || [ ! -x "$base/rating-service" ] || [ ! -r "$base/rating-ca.pem" ]; then sleep 5; continue; fi
  STATION_HUB_DNS="$dns" SSL_CERT_FILE="$base/rating-ca.pem" GOMEMLIMIT=32MiB GOGC=50 "$base/rating-service" >/dev/null 2>&1 &
  child=$!
  wait "$child"
  child=""
  sleep 10
done
