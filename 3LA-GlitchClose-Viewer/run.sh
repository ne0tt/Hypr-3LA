#!/usr/bin/env bash
# Launch the 3LA-GlitchClose shader tuner in a standalone browser window.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# regenerate generated.js if shader.hpp / main.cpp changed since last run
make -C "$SCRIPT_DIR" >/dev/null

URL="file://$SCRIPT_DIR/index.html"

# The shader is GLSL ES 3.00, so the viewer needs WebGL2. --app gives a clean
# window with no tab bar or omnibox, which suits a tool.
for BROWSER in google-chrome-stable google-chrome chromium brave; do
    if command -v "$BROWSER" >/dev/null 2>&1; then
        exec "$BROWSER" --app="$URL" --window-size=1600,1050 "$@"
    fi
done

echo "No Chromium-family browser found; falling back to xdg-open." >&2
echo "The viewer needs WebGL2 -- Firefox works, older browsers will not." >&2
exec xdg-open "$URL"
