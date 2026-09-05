#!/usr/bin/env bash
# Launch a GUI Python program so its window appears on the HDMI monitor, even
# when this script is started from an SSH session on another machine.
#
#   ./run.sh tools/check_camera.py
#   ./run.sh -m src.main
set -euo pipefail

export XDG_RUNTIME_DIR="/run/user/$(id -u)"

# Raspberry Pi OS desktop runs Wayland. Find the compositor's socket rather than
# guessing its number, which changes between sessions.
if [ -z "${WAYLAND_DISPLAY:-}" ]; then
  for socket in "$XDG_RUNTIME_DIR"/wayland-*; do
    [ -S "$socket" ] || continue
    export WAYLAND_DISPLAY="$(basename "$socket")"
    break
  done
fi

# X11 fallback, used when the desktop is running Xorg instead of Wayland.
export DISPLAY="${DISPLAY:-:0}"

cd "$(dirname "$0")"
exec python3 "$@"
