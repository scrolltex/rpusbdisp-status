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
exit $fail
