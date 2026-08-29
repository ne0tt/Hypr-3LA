#!/usr/bin/env bash
# Startup fallback: ensure 3LA-GlitchClose is loaded and its settings applied.
# Run from hyprland.lua on hyprland.start; safe to run repeatedly.
set -euo pipefail

sleep 2

DIR="$(cd "$(dirname "$0")" && pwd)"

if ! hyprctl plugin list | grep -q "3LA-GlitchClose"; then
    hyprctl plugin load "$DIR/3LA-GlitchClose.so"
fi

# re-apply plugin settings if they live in a Lua module (guarded by pcall
# inside plugins.lua) — adapt the path to your own config layout
PLUGCONF="$HOME/.config/hypr/config/plugins.lua"
[ -f "$PLUGCONF" ] && hyprctl eval 'dofile(os.getenv("HOME") .. "/.config/hypr/config/plugins.lua")'
