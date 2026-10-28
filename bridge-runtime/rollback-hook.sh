#!/system/bin/sh
set -eu
base=/data/local/stationhub-bridge
file=/system/vendor/quasar/run.sh
backup="$base/run.sh.original"
expected="$base/run.sh.expected"
new=/system/vendor/quasar/.run.sh.stationhub-restore
[ -f "$backup" ] || { echo no_backup; exit 1; }
[ ! -e "$new" ] || { echo staging_exists; exit 1; }
cp "$backup" "$expected"
cat >> "$expected" <<'HOOK'

# STATION_HUB_AUTOSTART_BEGIN
if [ -x /data/local/stationhub-bridge/boot-start.sh ]; then
    nohup /system/bin/sh /data/local/stationhub-bridge/boot-start.sh >/dev/null 2>&1 </dev/null &
fi
# STATION_HUB_AUTOSTART_END
HOOK
cmp "$expected" "$file" >/dev/null || { rm -f "$expected"; echo current_script_changed; exit 1; }
rm -f "$expected"
if [ "${1:-}" = "--check" ]; then echo HOOK_MATCHES; exit 0; fi
mount -o rw,remount /system || { echo remount_failed; exit 1; }
trap 'mount -o ro,remount /system >/dev/null 2>&1' EXIT INT TERM
cp "$backup" "$new"
/system/bin/sh -n "$new" || { rm -f "$new"; echo syntax_failed; exit 1; }
chown root:shell "$new"
chmod 755 "$new"
chcon u:object_r:system_file:s0 "$new" || { rm -f "$new"; echo label_failed; exit 1; }
mv "$new" "$file"
sync
echo HOOK_REMOVED
