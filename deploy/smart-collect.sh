#!/usr/bin/env bash
# Collects SMART health for rpusbdisp-status. Runs as root from
# rpusbdisp-smart.timer; the display program only reads the JSON files.
#   OUT_DIR   where <name>.json files go (default /run/rpusbdisp-status/smart)
#   DEVICES   devices to query (default: NVMe controllers and SATA/SAS disks)
#   SMARTCTL  smartctl binary (default: smartctl)
set -uo pipefail

OUT_DIR=${OUT_DIR:-/run/rpusbdisp-status/smart}
SMARTCTL=${SMARTCTL:-smartctl}
if [[ -z "${DEVICES:-}" ]]; then
    DEVICES=$(ls -d /dev/nvme[0-9] /dev/nvme[0-9][0-9] /dev/sd[a-z] 2>/dev/null | tr '\n' ' ')
fi

mkdir -p "$OUT_DIR"
chmod 0755 "$OUT_DIR"

for dev in $DEVICES; do
    name=$(basename "$dev")
    tmp="$OUT_DIR/.$name.json.tmp"
    # smartctl's exit status is a bit mask that is often non-zero on healthy
    # disks, so only the presence of output decides.
    # -n standby: never spin up a sleeping HDD just to read SMART.
    "$SMARTCTL" -n standby --json=c -H -A "$dev" > "$tmp" 2>/dev/null
    if grep -qi 'standby mode' "$tmp"; then
        # Asleep: keep the last result and tell the display why it is old.
        rm -f "$tmp"
        touch "$OUT_DIR/$name.standby"
        chmod 0644 "$OUT_DIR/$name.standby"
        continue
    fi
    # Error output from a disk smartctl cannot open (dying drive) has no
    # smart_status; keep the last good file so the badge goes stale (?)
    # instead of disappearing.
    if [[ -s "$tmp" ]] && grep -q '"smart_status"' "$tmp"; then
        chmod 0644 "$tmp"
        mv -f "$tmp" "$OUT_DIR/$name.json"
        rm -f "$OUT_DIR/$name.standby"
    else
        rm -f "$tmp"
    fi
done
exit 0
