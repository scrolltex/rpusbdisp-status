#!/usr/bin/env bash
# Installs rpusbdisp-status as a systemd service. Run from the repo root
# after building:  sudo ./deploy/install.sh
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "run as root: sudo $0" >&2
    exit 1
fi

BUILD_DIR=${BUILD_DIR:-build}
if [[ ! -x "$BUILD_DIR/rpusbdisp-status" ]]; then
    echo "$BUILD_DIR/rpusbdisp-status not found; build first (see README)" >&2
    exit 1
fi

if ! id statusdisp &>/dev/null; then
    useradd --system --no-create-home --shell /usr/sbin/nologin statusdisp
fi
getent group plugdev >/dev/null || groupadd --system plugdev
usermod -aG plugdev statusdisp
if getent group docker >/dev/null; then
    usermod -aG docker statusdisp
else
    echo "warning: group 'docker' not found; the Docker screen will show 'unavailable'" >&2
fi

cmake --install "$BUILD_DIR" --prefix /usr/local

udevadm control --reload
udevadm trigger --subsystem-match=usb --attr-match=idVendor=fccf

systemctl daemon-reload
systemctl enable status-display.service
systemctl restart status-display.service
systemctl --no-pager --lines=5 status status-display.service || true
