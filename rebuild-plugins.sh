#!/usr/bin/env bash
# Rebuild every plugin against the currently installed Hyprland headers.
#
# Each plugin bakes in the Hyprland API commit hash at compile time and
# refuses to load if it doesn't match the running compositor's, so every
# plugin needs a fresh build (well after `make clean`, so stale objects
# can't hide a real mismatch) whenever Hyprland is updated.
#
# A plugin whose build fails keeps its previous .so: it is copied aside
# before `make clean` and put back on failure, so a build broken by an API
# change never leaves the path hyprland.lua loads from empty.
#
# Usage:
#   ./rebuild-plugins.sh            # clean + build every plugin
#   ./rebuild-plugins.sh --reload   # also unload/load any plugin that's
#                                    # currently running in the compositor
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
RELOAD=0
[[ "${1:-}" == "--reload" ]] && RELOAD=1

if command -v pkg-config >/dev/null && pkg-config --exists hyprland; then
    echo "Building against Hyprland headers: $(pkg-config --modversion hyprland)"
else
    echo "warning: pkg-config can't find hyprland.pc — build will fail" >&2
fi
echo

plugins=()
for makefile in "$DIR"/3LA-*/Makefile; do
    plugin_dir="$(dirname "$makefile")"
    [[ "$plugin_dir" == *-Viewer ]] && continue
    plugins+=("$plugin_dir")
done

failed=()
built=()

backup_dir="$(mktemp -d)"
trap 'rm -rf "$backup_dir"' EXIT

for plugin_dir in "${plugins[@]}"; do
    name="$(basename "$plugin_dir")"
    so="$plugin_dir/$name.so"
    backup="$backup_dir/$name.so"
    echo "==> $name"
    if [[ -f "$so" ]]; then
        cp -p "$so" "$backup"
    fi
    if make -C "$plugin_dir" clean >/dev/null && make -C "$plugin_dir"; then
        built+=("$name")
        echo "    built: $so ($(du -h "$so" | cut -f1))"
    else
        failed+=("$name")
        if [[ -f "$backup" ]]; then
            cp -p "$backup" "$so"
            echo "    FAILED: $name (previous $name.so restored)" >&2
        else
            echo "    FAILED: $name (no previous $name.so to restore)" >&2
        fi
    fi
    echo
done

if [[ "$RELOAD" -eq 1 ]]; then
    loaded="$(hyprctl plugin list 2>/dev/null || true)"
    for name in "${built[@]}"; do
        so="$DIR/$name/$name.so"
        if grep -q "$name" <<<"$loaded"; then
            echo "==> reloading $name into the running compositor"
            hyprctl plugin unload "$so" >/dev/null
            hyprctl plugin load "$so" >/dev/null
        fi
    done
    echo
fi

echo "Built:  ${built[*]:-none}"
if [[ "${#failed[@]}" -gt 0 ]]; then
    echo "Failed: ${failed[*]}" >&2
    exit 1
fi
