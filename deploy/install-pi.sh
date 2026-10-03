#!/bin/sh
# Build HandWave on the Pi, install it to /opt/handwave, and (optionally)
# make the Pi boot straight into it.
#
# Usage (from the source tree on the Pi):
#   ./deploy/install-pi.sh            build + install, don't touch boot
#   ./deploy/install-pi.sh --kiosk    ...and enable boot-to-app
#   ./deploy/install-pi.sh --no-kiosk ...and restore the normal login prompt
set -eu

cd "$(dirname "$0")/.."
SRC=$(pwd)
PREFIX=/opt/handwave

echo "== Building (one job at a time: the Pi 3 A+ has 512 MB RAM)"
cmake --preset pi -DHANDWAVE_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build --preset pi -j1

echo "== Installing to $PREFIX"
sudo cmake --install build/pi
sudo install -m 644 deploy/kms.json "$PREFIX/kms.json"
sudo install -m 644 deploy/handwave.service /etc/systemd/system/handwave.service
sudo systemctl daemon-reload

case "${1:-}" in
--kiosk)
    echo "== Enabling boot-to-app"
    sudo systemctl disable --now getty@tty1.service
    sudo systemctl enable handwave.service
    # Quiet boot: no kernel text, no blinking console cursor on the panel.
    CMDLINE=/boot/firmware/cmdline.txt
    sudo cp -n "$CMDLINE" "$CMDLINE.handwave-backup"
    sudo sed -i 's/ console=tty1/ console=tty3/' "$CMDLINE"
    for opt in quiet loglevel=3 logo.nologo vt.global_cursor_default=0; do
        grep -qw "$opt" "$CMDLINE" || sudo sed -i "1 s/\$/ $opt/" "$CMDLINE"
    done
    # cloud-init applies Raspberry Pi Imager's settings on first boot only, but
    # still runs (and blocks boot) every time. Re-enable: delete this file.
    [ -d /etc/cloud ] && sudo touch /etc/cloud/cloud-init.disabled
    sudo systemctl restart handwave.service
    ;;
--no-kiosk)
    echo "== Restoring normal console login"
    sudo systemctl disable --now handwave.service
    sudo systemctl enable --now getty@tty1.service
    CMDLINE=/boot/firmware/cmdline.txt
    [ -f "$CMDLINE.handwave-backup" ] && sudo cp "$CMDLINE.handwave-backup" "$CMDLINE"
    sudo rm -f /etc/cloud/cloud-init.disabled
    ;;
esac

echo "== Done. Logs: journalctl -u handwave -f"
