#!/usr/bin/env bash
set -euo pipefail

sudo cp packaging/udev/70-taskautomation.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
sudo usermod -aG input "$USER"

echo "Installed development udev rule. Log out and back in for group changes to apply."
echo "Then run ./build/taskautomation as your normal user, not with sudo."
