#!/system/bin/sh
base=${STATION_HUB_HOME:-/data/local/stationhub-bridge}
unset LD_PRELOAD
umask 077
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24; do
  [ "$(getprop sys.boot_completed)" = 1 ] && break
  sleep 5
done
[ "$(getprop sys.boot_completed)" = 1 ] || exit 1
# This firmware blocks ordinary BOOT_COMPLETED receivers. Start our service as root.
am startservice -n ru.stationhub.app/.AudioReceiverService >/dev/null 2>&1
# Rating access is independent of the optional voice bridge and its crash guard.
if [ -x "$base/rating-supervisor.sh" ]; then "$base/rating-supervisor.sh" >/dev/null 2>&1 & fi
[ -e "$base/disabled" ] && exit 0
boot=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null)
[ -n "$boot" ] || exit 1
pending="$base/boot-pending"
if [ -f "$pending" ]; then
  previous=$(cat "$pending" 2>/dev/null)
  if [ -n "$previous" ] && [ "$previous" != "$boot" ]; then
    : > "$base/disabled"
    echo previous_boot_unstable > "$base/status"
    sync
    exit 0
  fi
fi
printf '%s\n' "$boot" > "$pending"
sync
/system/bin/sh "$base/bootstrap.sh" >/dev/null 2>&1 &
# Clear the crash guard only after five minutes with all local services healthy.
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60; do
  [ -e "$base/disabled" ] && exit 0
  sleep 5
done
pid=$(cat "$base/supervisor.lock/pid" 2>/dev/null)
cmd=""
[ -n "$pid" ] && cmd=$(tr "\000" " " < "/proc/$pid/cmdline" 2>/dev/null)
case "$cmd" in *"$base/bootstrap.sh"*) supervisor_ok=1;; *) supervisor_ok=0;; esac
if [ "$supervisor_ok" = 1 ] && kill -0 "$pid" 2>/dev/null && "$base/voice-pilot" -mode check-hub >/dev/null 2>&1 && /system/bin/curl -fsS --max-time 2 http://127.0.0.1:18443/healthz >/dev/null 2>&1; then
  [ "$(cat "$pending" 2>/dev/null)" = "$boot" ] && rm -f "$pending"
  echo ready > "$base/status"
  sync
fi
