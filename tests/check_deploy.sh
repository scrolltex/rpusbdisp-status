#!/usr/bin/env bash
# Static checks for deploy files (run by ctest).
set -euo pipefail
unit="$1/deploy/status-display.service"
fail=0
# A missing group in SupplementaryGroups= makes systemd refuse to start the unit
# (status 216/GROUP). Group membership comes from the statusdisp user instead.
if grep -q '^SupplementaryGroups=' "$unit"; then echo "FAIL: unit uses SupplementaryGroups="; fail=1; fi
# Bound shutdown even if the USB status thread is slow to stop.
if ! grep -q '^TimeoutStopSec=' "$unit"; then echo "FAIL: unit lacks TimeoutStopSec="; fail=1; fi
bash -n "$1/deploy/install.sh" || fail=1

# statvfs on a separate /home mount needs read access.
if ! grep -q '^ProtectHome=read-only' "$unit"; then echo "FAIL: unit needs ProtectHome=read-only"; fail=1; fi

# SMART timer: every 10 minutes, script syntax.
timer="$1/deploy/rpusbdisp-smart.timer"
script="$1/deploy/smart-collect.sh"
if ! grep -q '^OnUnitActiveSec=10min' "$timer" 2>/dev/null; then echo "FAIL: smart timer must run every 10min"; fail=1; fi
if [[ ! -f "$1/deploy/rpusbdisp-smart.service" ]]; then echo "FAIL: missing rpusbdisp-smart.service"; fail=1; fi
bash -n "$script" 2>/dev/null || { echo "FAIL: smart-collect.sh missing or has syntax errors"; fail=1; }

# smart-collect.sh with a fake smartctl: writes JSON per device, skips empty output.
if [[ -f "$script" ]]; then
    tmp=$(mktemp -d)
    printf '#!/bin/sh\necho "{\\"smart_status\\":{\\"passed\\":true}}"\nexit 4\n' > "$tmp/smartctl-ok"
    printf '#!/bin/sh\nexit 2\n' > "$tmp/smartctl-empty"
    chmod +x "$tmp/smartctl-ok" "$tmp/smartctl-empty"
    SMARTCTL="$tmp/smartctl-ok" OUT_DIR="$tmp/out" DEVICES="/dev/nvme0 /dev/sda" bash "$script"
    grep -q passed "$tmp/out/nvme0.json" 2>/dev/null || { echo "FAIL: nvme0.json not written"; fail=1; }
    grep -q passed "$tmp/out/sda.json" 2>/dev/null || { echo "FAIL: sda.json not written"; fail=1; }
    SMARTCTL="$tmp/smartctl-empty" OUT_DIR="$tmp/out2" DEVICES="/dev/nvme0" bash "$script"
    [[ ! -e "$tmp/out2/nvme0.json" ]] || { echo "FAIL: empty smartctl output must not be written"; fail=1; }
    # A dying disk smartctl cannot open: error JSON without smart_status must
    # not replace the last good result (the badge then goes stale instead).
    printf '#!/bin/sh\necho "{\\"smartctl\\":{\\"exit_status\\":2}}"\nexit 2\n' > "$tmp/smartctl-err"
    chmod +x "$tmp/smartctl-err"
    SMARTCTL="$tmp/smartctl-err" OUT_DIR="$tmp/out" DEVICES="/dev/nvme0" bash "$script"
    grep -q passed "$tmp/out/nvme0.json" 2>/dev/null || { echo "FAIL: error output replaced a good SMART result"; fail=1; }
    rm -rf "$tmp"
fi
exit $fail
