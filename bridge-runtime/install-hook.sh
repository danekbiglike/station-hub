#!/system/bin/sh
set -eu
base=/data/local/stationhub-bridge
file=/system/vendor/quasar/run.sh
new=/system/vendor/quasar/.run.sh.stationhub-new
backup="$base/run.sh.original"
[ -x "$base/boot-start.sh" ] || { echo no_boot_script; exit 1; }
[ ! -e "$backup" ] || { echo backup_exists; exit 1; }
[ ! -e "$new" ] || { echo staging_exists; exit 1; }
if grep -q STATION_HUB_AUTOSTART_BEGIN "$file"; then echo hook_exists; exit 1; fi
cp -p "$file" "$backup"
cmp "$file" "$backup" >/dev/null || { echo backup_mismatch; exit 1; }
chmod 600 "$backup"
sync
mount -o rw,remount /system || { echo remount_failed; exit 1; }
trap 'mount -o ro,remount /system >/dev/null 2>&1' EXIT INT TERM
cp -p "$file" "$new"
cat >> "$new" <<'HOOK'

# STATION_HUB_AUTOSTART_BEGIN
if [ -x /data/local/stationhub-bridge/boot-start.sh ]; then
    nohup /system/bin/sh /data/local/stationhub-bridge/boot-start.sh >/dev/null 2>&1 </dev/null &
fi
# STATION_HUB_AUTOSTART_END
HOOK
/system/bin/sh -n "$new" || { rm -f "$new"; echo syntax_failed; exit 1; }
chown root:shell "$new"
chmod 755 "$new"
chcon u:object_r:system_file:s0 "$new" || { rm -f "$new"; echo label_failed; exit 1; }
mv "$new" "$file"
sync
echo HOOK_INSTALLED
