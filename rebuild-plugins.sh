#!/usr/bin/env bash
# Rebuild every plugin against the currently installed Hyprland headers,
# then load the fresh builds into the running compositor.
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
# After building, every plugin that built is loaded, unloading it first if
# it's running, then the plugin settings file is re-applied through
# `hyprctl eval`. Option values outlive an unload, so without that re-apply
# a reloaded plugin keeps stale values and options a rebuild adds stay at
# defaults. The settings file is Lua (needs a Lua Hyprland config); a
# missing file is skipped. A plugin whose build failed is left as it is in
# the compositor. With no running Hyprland to reach (e.g. from a TTY), the
# plugins are still built and the reload is skipped, exiting non-zero.
#
# Usage:
#   ./rebuild-plugins.sh            # clean + build every plugin, then
#                                    # load each and re-apply its settings
#
# Environment:
#   HYPR_PLUGIN_SETTINGS   settings file re-applied after loading
#                          (default: ~/.config/hypr/config/plugins.lua)
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
SETTINGS="${HYPR_PLUGIN_SETTINGS:-$HOME/.config/hypr/config/plugins.lua}"
RELOAD=1
if ! hyprctl version >/dev/null 2>&1; then
    echo "warning: can't reach a running Hyprland (is HYPRLAND_INSTANCE_SIGNATURE set?); building only" >&2
    RELOAD=0
fi

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

is_loaded() {
    grep -q "^Plugin $1 by " <<<"$(hyprctl plugin list)"
}

load_failed=()
loaded=()
settings_failed=0

if [[ "$RELOAD" -eq 1 ]]; then
    for name in "${built[@]}"; do
        so="$DIR/$name/$name.so"
        echo "==> loading $name"
        # The same path can't be loaded twice.
        if is_loaded "$name"; then
            hyprctl plugin unload "$so" >/dev/null
        fi
        # hyprctl exits 0 on a failed load; only "ok" means it loaded.
        out="$(hyprctl plugin load "$so")"
        if [[ "$out" == "ok" ]] && is_loaded "$name"; then
            loaded+=("$name")
        else
            load_failed+=("$name")
            echo "    FAILED: $out" >&2
        fi
    done
    echo

    if [[ "${#loaded[@]}" -gt 0 ]]; then
        if [[ -f "$SETTINGS" ]]; then
            echo "==> applying settings: $SETTINGS"
            if ! out="$(hyprctl eval "dofile([[$SETTINGS]])" 2>&1)"; then
                settings_failed=1
                echo "    FAILED: $out" >&2
            fi
            errors="$(hyprctl configerrors)"
            if [[ -n "$errors" ]]; then
                settings_failed=1
                echo "    config errors:" >&2
                echo "$errors" >&2
            fi
        else
            echo "==> no settings file at $SETTINGS (set HYPR_PLUGIN_SETTINGS); skipped"
        fi
        echo
    fi
fi

echo "Built:  ${built[*]:-none}"
status=0
if [[ "$RELOAD" -eq 1 ]]; then
    echo "Loaded: ${loaded[*]:-none}"
else
    echo "Reload skipped: no running Hyprland" >&2
    status=1
fi
if [[ "${#failed[@]}" -gt 0 ]]; then
    echo "Failed to build: ${failed[*]}" >&2
    status=1
fi
if [[ "${#load_failed[@]}" -gt 0 ]]; then
    echo "Failed to load:  ${load_failed[*]}" >&2
    status=1
fi
if [[ "$settings_failed" -eq 1 ]]; then
    echo "Settings: errors applying $SETTINGS" >&2
    status=1
fi
exit "$status"
