# 3LA-GlitchClose viewer

A WebGL2 tuner for the [3LA-GlitchClose](../3LA-GlitchClose) shader. Scrub the
close animation over a still image, move the sliders, copy the result straight
into `plugins.lua` — instead of closing a terminal every time you want to see a
value change. Not a compositor plugin; `rebuild-plugins.sh` skips it.

## Launching

```sh
./run.sh                 # regenerate if needed, then open a browser window
make open                # same thing
make install-desktop     # adds "3LA-GlitchClose Tuner" to the app launcher
```

`run.sh` runs `make` first, so it picks up changes to `shader.hpp` or
`main.cpp`, and exits if that build fails. It then opens `index.html` as an
`--app` window (no tab bar or omnibox, 1600x1050) in the first of
`google-chrome-stable`, `google-chrome`, `chromium`, `brave` it finds, passing
extra arguments through; with none of them it falls back to `xdg-open`. Any
WebGL2 browser works, Firefox included.

Opening `index.html` directly also works, without the regenerate step:
`generated.js` is committed, is plain globals rather than a module, and embeds
the background as a data URI, so `file://` needs no web server.

`install-desktop` writes `~/.local/share/applications/3LA-GlitchClose-Viewer.desktop`
with `Exec` set to this checkout's `run.sh` and the theme icon
`applications-graphics`, then runs `update-desktop-database`.

For a keybind, point it at `run.sh`:

```lua
hl.bind(mainMod .. " + SHIFT + G",
    hl.dsp.exec_cmd("/path/to/Hypr-3LA/3LA-GlitchClose-Viewer/run.sh"),
    { description = "GlitchClose shader tuner" })
```

Other targets:

```sh
make        # regenerate generated.js and .build/ from the plugin sources
make check  # compile the extracted GLSL with glslangValidator, no browser
make clean  # delete generated.js and .build/
```

Requires `python` (stdlib only) and `make`; `glslang` for `make check`;
`desktop-file-utils` for `install-desktop` (skipped silently if missing).

## Saved settings

Every slider, colour and toggle (including **loop** and the simulation
controls) is written to `localStorage` as you move it, and restored next time
you open the viewer — including the last image you dropped in or picked. This
is **viewer state only**; nothing is written to the plugin. Exporting to
`plugins.lua` stays a deliberate *copy Lua* → paste.

*reset to plugin defaults* clears the saved state, sets every option back to
the default `main.cpp` registers (colours to the **matugen** preset) and
reloads the bundled image. The simulation toggles and **loop** are left as
they are.

The payload is versioned, and only keys the plugin still registers are restored
— so removing an option from `main.cpp` cannot resurrect a stale value. If an
image pushes the payload past the storage quota, the image is dropped and the
settings are kept rather than losing both.

## It runs the plugin's actual shader

The viewer has **no shader of its own**. `sync.py` extracts, from
`../3LA-GlitchClose/`:

- `GLITCH_VERT` / `GLITCH_FRAG` out of `shader.hpp`
- the config option list — keys, descriptions, defaults, min/max — out of
  `main.cpp`

into `generated.js` (and `.build/glitch.{vert,frag}` for `make check`). Slider
ranges are the plugin's own; where it leaves a maximum open (`duration`,
`fade`, `text_size`, `text_blink`, `text_padding`, `text_bg_round`) the ceiling
comes from `MAX_HINT` in `sync.py`. `enabled`, `hold` and the window filters
(`min_size`, `ignore_*`) get no control and are left out of the export.

`sync.py` fails the build if a shader slider or colour key has no matching
`uniform` in `GLITCH_FRAG` (key → camelCase, `noise` → `noiseAmount`), or if
fewer than 8 sliders parse. Timeline and caption keys are exempt. Uploading is
separate: `index.html` looks up and sets its own fixed list of uniforms in
`frame()`, so a new shader knob also needs adding there.

Re-run `make` after editing `shader.hpp` or `main.cpp`.

## Matching the compositor

Reproduced:

- **Premultiplied alpha.** The shader writes `vec4(rgb * a, a)`, so the viewer
  blends with `ONE, ONE_MINUS_SRC_ALPHA`.
- **Timeline.** `progress = elapsed / duration`, full strength for `duration`,
  then a linear `fade`. The plugin starts the fade once the window is gone
  (capped by `hold`); a close sent at `close_at * duration` ≤ `duration` is
  normally gone by then, so the viewer starts the fade at `duration` and has no
  `hold`.

Not reproduced:

- **Y orientation.** The viewer draws to the default framebuffer and uploads the
  image with `UNPACK_FLIP_Y_WEBGL`, so `v_texcoord.y == 0` is the *bottom* of
  the canvas; in the plugin it is the *top* of the window (`shader.hpp` header).
  The image is upright in both, but the tear seam, roll bar and melt run
  vertically mirrored relative to the compositor.
- **The snapshot.** Tears reveal a dimmed copy of the same image, not your
  actual desktop.
- **Window filtering** (`min_size`, `ignore_*`), which is not visual.

The **caption** is an approximation. The plugin composites a rect plus a
`renderText()` texture, shrunk to fit 80% of the window's width and 40% of its
height; the viewer uses a positioned HTML element with no fit, rasterised by
the browser, so the glyphs will not be pixel-identical. Size, colour, padding,
corner radius and timing (`text_at`, `text_blink`, hidden during the fade) are
close enough to tune with.

## Controls

- **space** play/pause · **r** restart · **n** new seed (ignored while a
  control has focus)
- **loop** replays with a new seed each pass; dragging the scrub bar pauses on
  that frame. The readout shows burst ms and the `close_at` time, then fade ms
  and the envelope
- Drag an image onto the canvas, or use the file picker, to swap the source
- **window captured** off simulates a failed snapshot (`hasTex = 0`): no window
  pixels, static and backdrop only
- **desktop behind tears** / **backdrop brightness** toggle and dim the copy of
  the image drawn underneath
- **matugen** / **stock** preset all five colours: *stock* is the plugin's
  built-in set (what `0` selects), *matugen* is a fixed palette in
  `index.html`, not read from the live theme
- **copy Lua** copies the block shown under it: a
  `pcall(hl.config, { plugin = { ["3la_glitch_close"] = { … } } })` with every
  key the viewer shows. Colours are `0xffRRGGBB` literals, so pasting it over
  the block in `~/.config/hypr/config/plugins.lua` replaces any `colors.lua`
  variables there. Apply with `hyprctl reload`, or try it live with
  `hyprctl eval '<block>'` (undone by the next `hyprctl reload`;
  `hyprctl keyword` does not work with a Lua config)

The bundled background is a sample terminal screenshot. Replace
`background-src.jpg` (JPEG, same name) and re-run `make` to change the default.
