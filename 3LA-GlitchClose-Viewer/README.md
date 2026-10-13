# 3LA-GlitchClose viewer

A WebGL2 tuner for the [3LA-GlitchClose](../3LA-GlitchClose) shader. Scrub the
close animation over a still image, move the sliders, copy the result straight
into `plugins.lua` — instead of closing a terminal every time you want to see a
value change.

## Launching

```sh
./run.sh                 # standalone browser window (regenerates first)
make open                # same thing
make install-desktop     # adds "3LA-GlitchClose Tuner" to the app launcher
```

`run.sh` runs `make` first, so it always picks up changes to `shader.hpp` or
`main.cpp`, then opens a Chromium `--app` window (no tab bar or omnibox). It
falls back through chromium/brave to `xdg-open`. Double-clicking `index.html`
also works — everything is inlined, no web server needed.

For a keybind, point it at `run.sh`:

```lua
hl.bind(mainMod .. " + SHIFT + G",
    hl.dsp.exec_cmd(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-GlitchClose-Viewer/run.sh"),
    { description = "GlitchClose shader tuner" })
```

Other targets:

```sh
make        # regenerate generated.js from the plugin sources
make check  # compile the extracted GLSL with glslangValidator, no browser
```

## Saved settings

Every slider, colour and toggle is written to `localStorage` as you move it, and
restored next time you open the viewer — including the last image you dropped in.
This is **viewer state only**; nothing is written to the plugin. Exporting to
`plugins.lua` stays a deliberate *copy Lua* → paste.

*reset to plugin defaults* clears the saved state and goes back to the values
`main.cpp` registers.

The payload is versioned, and only keys the plugin still registers are restored
— so removing an option from `main.cpp` cannot resurrect a stale value. If a
dropped image pushes the payload past the storage quota, the image is dropped
and the settings are kept rather than losing both.

## It runs the plugin's actual shader

The viewer has **no shader of its own**. `sync.py` extracts, from
`../3LA-GlitchClose/`:

- `GLITCH_VERT` / `GLITCH_FRAG` out of `shader.hpp`
- the config option list — keys, defaults, min/max — out of `main.cpp`

so the sliders and their ranges are whatever the plugin currently registers. A
copied shader would drift, and a tuner that shows something other than what the
compositor draws is worse than no tuner at all.

`sync.py` fails the build if a config key has no matching uniform in the shader
(it caught `backdrop_alpha` vs `backdropAlpha` when the viewer was written), so
adding a knob to the plugin and forgetting to wire it up is a build error rather
than a dead slider.

Re-run `make` after editing `shader.hpp` or `main.cpp`.

## Matching the compositor

Three details are reproduced deliberately, because getting them wrong would
make the preview lie:

- **Y orientation.** The plugin samples a framebuffer texture, stored bottom-up,
  so `v_texcoord.y == 0` is the *bottom* of the window. The viewer sets
  `UNPACK_FLIP_Y_WEBGL` to get the same convention from a top-down HTML image,
  which leaves `uvOffset`/`uvXf` at identity.
- **Premultiplied alpha.** The shader writes `vec4(rgb * a, a)`, so the viewer
  blends with `ONE, ONE_MINUS_SRC_ALPHA` — the same thing `CTexPassElement` does.
- **Timeline.** Full strength for `duration`, then `fade`. The plugin waits for
  the window to actually vanish before starting the tail; a well-behaved app
  goes at `close_at * duration`, which is ≤ `duration`, so the fade starts at
  `duration` here.

What it does *not* model: the real snapshot (tears reveal a dimmed copy of the
same image rather than your actual desktop), and per-window filtering
(`min_size`, `ignore_class`, …), which are not visual.

The **caption** is an approximation. The plugin composites a rect plus a texture
from `renderText()`; the viewer uses a positioned HTML element, so the glyphs are
rasterised by the browser rather than by Hyprland and will not be pixel-identical.
Size, colour, padding, corner radius and timing are faithful enough to tune with.

## Controls

- **space** play/pause · **r** restart · **n** new seed
- Drag an image onto the canvas, or use the file picker, to swap the source
- **window captured** off simulates a failed snapshot — the `hasTex = 0`
  static-only path a post-hoc close usually takes
- **matugen** / **stock** preset the three colours
- **copy Lua** emits a `pcall(hl.config, ...)` block ready to paste

The bundled background is a screenshot of this desktop. Replace
`background-src.jpg` and re-run `make` to change the default.
