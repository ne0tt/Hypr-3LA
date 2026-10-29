# 3LA-GlitchClose

Plays a CCTV "signal lost" collapse over a window when it closes: the window's
own content tears apart, static and a backdrop ramp in over it, a
`SIGNAL LOST` caption appears, and only then does the window actually close.

![3LA-GlitchClose](../assets/glitchclose-showcase.gif)

*A lone kitty closed with `hl.plugin.glitchclose.close()`, real time
(`duration 400`). ([mp4](../assets/glitchclose-showcase.mp4))*

![demo: three terminals closed with the shader glitch collapse](../assets/glitchclose-demo.gif)

*Three terminals framed by 3LA-Corners, closed one by one.
([mp4](../assets/glitchclose-demo.mp4))*

Apart from the caption, the whole visual is one GLSL fragment shader — the
window snapshot goes through a single pass into the effect's own framebuffer,
which is then composited. So:

- displacement is **per-pixel**; the tear geometry holds for 1/16 of the burst
  at a time, so bands visibly tear instead of shimmering
- the v-sync frame tear is real: the content on each side of the seam is
  evaluated at a different animation step
- chromatic aberration is a genuine per-channel UV offset
- static is a per-effect seeded hash on a 2 px grid, re-rolled every step
- one shader draw call per effect per frame
- every shader knob and colour is a uniform read each frame, so re-running
  matugen re-themes a running effect without reloading the plugin

Part of [Hypr-3LA](../README.md).

## Contents

- [Build and load](#build-and-load)
- [Closing windows](#closing-windows)
- [Config](#config)
- [Tuning it](#tuning-it)
- [How it renders](#how-it-renders)
- [Notes](#notes)

## Build and load

Requires Hyprland 0.57.0 headers (see the [main README](../README.md#requirements)).
Draws with raw GL calls through Hyprland's GL renderer.

```sh
make                                              # → 3LA-GlitchClose.so (stripped)
hyprctl plugin load "$PWD/3LA-GlitchClose.so"     # absolute path required
hyprctl getoption plugin:3la_glitch_close:duration
```

At startup, in `hyprland.lua`:

```lua
hl.plugin.load("/path/to/Hypr-3LA/3LA-GlitchClose/3LA-GlitchClose.so")
```

`autoload.sh` is an optional startup fallback: it loads the `.so` if
`hyprctl plugin list` doesn't show it, then re-runs
`~/.config/hypr/config/plugins.lua` through `hyprctl eval`.

Or via hyprpm: `hyprpm enable 3LA-GlitchClose` (after `hyprpm add`, see the
[main README](../README.md#option-a--hyprpm)).

Rebuild after every Hyprland update: `make clean && make` (plain `make` only
tracks the plugin's own sources), or `./rebuild-plugins.sh` at the repo root,
which keeps the previous `.so` if a build fails. A build for a different
Hyprland commit refuses to load with a *version mismatch* notification.
`make debug` does a clean, unstripped build with `-g`.

## Closing windows

**Use `hl.plugin.glitchclose.close()` to close windows.** Closing first and
animating afterwards means the layout has already re-tiled and the effect
plays over the neighbours that took the space. `close()` fixes the ordering:
the collapse plays over the still-open window, and the real close goes out at
`close_at` × `duration` — by default as the burst ends.

```lua
-- falls through to a plain close when the plugin isn't loaded
hl.bind("SUPER + Q", function()
    if pcall(function() hl.plugin.glitchclose.close() end) then return end
    hl.dispatch(hl.dsp.window.close())
end, { description = "Close active window (glitch collapse)" })
```

`close()` takes no arguments and acts on the **focused** window. A window the
effect skips (see [Notes](#notes)) is closed straight away; calling it again
while that window's close is in flight does nothing.

From a shell: `hyprctl eval 'hl.plugin.glitchclose.close()'`. `hyprctl
dispatch` also runs it but then reports an error, because `close()` returns
no dispatcher.

**It always ends on a glitched window.** After the burst the overlay *holds*
at full collapse until the window is really gone, and only then runs the
`fade` tail — fading earlier would dissolve back onto the untouched window
still waiting to unmap. Two things make the hold work:

- the window's **own close animation is suppressed** (its `noanim` property,
  set at `setprop` priority) while the effect plays; otherwise Hyprland's
  fade-out snapshot looks like the window reappearing.
- `hold` caps the wait (default 1000 ms). An app that refuses to close — an
  unsaved-changes dialog — gets the fade anyway and its animations back.

The tile slot is held for the whole burst. Set `close_at` below 1 to send the
close mid-burst instead.

### Windows closed some other way

Any other close — `hl.dsp.window.close()`, an app quitting on its own, a plain
shell `exit` — still gets the effect: Hyprland emits `window.close` as the
window unmaps, and the plugin snapshots the window at that moment (if the
capture fails, the shader draws static and backdrop only). By then the window
has left the layout, so the neighbours re-tile at once, the collapse plays over
them, and the fade follows the burst with no hold.

**Held-slot close for shell exits:** shadow `exit` with a function that calls
`close()` before the shell terminates, falling back to a real exit when the
plugin isn't loaded:

```zsh
function exit() {
    if (( ZSH_SUBSHELL == 0 )) &&
       [[ "$(hyprctl repl 'type((hl.plugin or {}).glitchclose)' 2>/dev/null)" == table ]]; then
        hyprctl eval 'hl.plugin.glitchclose.close()' >/dev/null
    else
        builtin exit "$@"
    fi
}
```

`hyprctl repl` prints the expression's value (`table` while the plugin is
loaded). `close()` acts on the *focused* window, so this assumes the terminal
running `exit` is focused; the `ZSH_SUBSHELL` check keeps `(exit 1)` and
similar from closing the terminal.

## Config

Defaults shown:

```lua
pcall(hl.config, {
    plugin = {
        ["3la_glitch_close"] = {
            enabled = 1,           -- 0 = off: windows close normally with no
                                   -- effect; close() still closes
            duration = 700,        -- collapse duration (ms, min 100)
            fade = 80,             -- fade-out tail (ms), started only once the
                                   -- window is actually gone
            hold = 1000,           -- max ms to hold the finished collapse while
                                   -- waiting for the window to vanish
            close_at = 1.0,        -- when close() sends the real close, as a
                                   -- fraction of `duration`

            strength = 1.0,        -- master multiplier on every displacement
                                   -- term (0 = calm, up to 5 = extreme)
            aberration = 0.5,      -- RGB channel split
            blocks = 0.6,          -- slice tearing + macroblock corruption
            noise = 0.5,           -- digital static
            scanlines = 0.4,       -- CRT scanline darkening
            roll = 0.5,            -- rolling bright bar sweeping the window
            melt = 0.5,            -- wavy vertical tear boundary; 0 = flat shear
            tear = 0.5,            -- v-sync frame tear (0 = off)
            tear_speed = 3.0,      -- seam sweeps per burst (0–12)
            ghost = 0.5,           -- whole-frame echo copies (0 = off)
            vignette = 0.4,        -- edge darkening as the feed collapses
            backdrop_alpha = 0.75, -- opacity the backdrop collapses to

            ["col.backdrop"] = 0,  -- 0 = black
            ["col.fringe1"] = 0,   -- 0 = magenta
            ["col.fringe2"] = 0,   -- 0 = cyan

            -- Caption drawn OVER the shader output (not fed through the
            -- glitch, so it stays legible). Burst and hold, never the fade.
            text = "SIGNAL LOST",  -- "" turns the caption off entirely
            font = "monospace",
            text_size = 16,        -- pt (min 6)
            text_alpha = 1.0,      -- 0 = plate with no text
            text_at = 0.4,         -- fraction of duration before it appears
            text_blink = 0,        -- blink half-period in ms (0 = steady)
            text_padding = 14,     -- gap between text and plate edge (px)
            text_bg_round = 4,     -- plate corner radius (px)
            text_bg_alpha = 0.85,  -- 0 = text with no plate
            ["col.text"] = 0,      -- 0 = white
            ["col.text_bg"] = 0,   -- 0 = red

            min_size = 80,         -- skip windows smaller than this on either
                                   -- axis (px)
            ignore_children = 1,   -- skip dialogs, transients, modals
            ignore_class = "^(xdg-desktop-portal.*)$", -- portal file choosers
            ignore_title = "",
        }
    }
})
```

- Ranges are enforced by Hyprland: an out-of-range value is rejected with a
  config error and the previous value stays. Floats other than `strength` and
  `tear_speed` take 0–1; ints are ≥ 0 unless noted.
- Colours: `0` = the built-in default, otherwise a colour string such as
  `"rgba(1b434eff)"`. Alpha is ignored for `col.backdrop`, `col.fringe1`,
  `col.fringe2` and `col.text_bg`; use `backdrop_alpha` / `text_bg_alpha`.
- `pcall` stops a Lua error aborting the rest of the file, but does not
  suppress Hyprland's *unknown config key* error when the plugin isn't loaded.
  If the block can run without it, guard it with
  `if hl.get_config("plugin:3la_glitch_close:enabled") ~= nil then … end`.

### What the dials do

- `strength` scales *all* displacement; the individual weights are 0..1 dials
  on one shader stage each, so `strength = 2, blocks = 0` gives heavy melt and
  aberration with no slice displacement.
- `ghost` echoes the **whole frame** sideways at low alpha, added rather than
  blended so overlaps brighten — text comes out doubled. It is what makes a
  torn frame read as a *doubled signal*. Set `0` for clean shearing.
- `blocks` vs `tear`: `blocks` scatters many short-lived strips at random
  offsets. `tear` is a single horizontal **v-sync seam** sweeping up the
  window, with everything above it shifted sideways and evaluated one
  animation step ahead, so it reads as a frame boundary. `tear_speed` sets how
  many times the seam crosses; with a short `duration`, 1–2 reads as a glitch
  and higher as a strobe.
- The caption is composited on top, drawn through the burst and hold but
  never the fade, and scaled down to fit 80 % of the window's width and 40 %
  of its height. `text_at` is a fraction of `duration` — with a short
  `duration` the default `0.4` leaves little time on screen.

### Example config

A short, punchy burst themed from matugen colour globals:

```lua
pcall(hl.config, {
    plugin = {
        ["3la_glitch_close"] = {
            duration = 400,
            fade = 10,
            strength = 0.38,
            aberration = 0.56,
            blocks = 0.45,
            noise = 0.0,
            scanlines = 0.8,
            roll = 0.08,
            melt = 0.10,
            tear = 0.55,
            tear_speed = 2.0,
            ghost = 0.8,
            vignette = 0.0,
            backdrop_alpha = 0.5,
            ["col.backdrop"] = secondary_container,
            ["col.fringe1"] = tertiary,
            ["col.fringe2"] = primary,
            text_size = 20,
            text_alpha = 0.85,
            text_at = 0.15,
            text_blink = 10,
            text_bg_round = 0,
            text_bg_alpha = 0.55,
            ["col.text_bg"] = tertiary,
        }
    }
})
```

The shader colours and the plate colour are read every frame, so re-running
matugen re-themes a live effect with no plugin reload. `text`, `font`,
`text_size` and `col.text` are rendered into a cached caption texture that is
rebuilt only on a config reload: change them in the config file and
`hyprctl reload`. A `hyprctl eval` change to those four doesn't reach a
caption that has already been drawn.

## Tuning it

[`3LA-GlitchClose-Viewer/`](../3LA-GlitchClose-Viewer) is a WebGL2 tuner that
runs this plugin's actual shader over a still image: scrub the close animation
on a timeline, move the sliders, then hit **copy Lua** for a ready-to-paste
`pcall(hl.config, …)` block.

```sh
make -C ../3LA-GlitchClose-Viewer open            # build the bundle and open it
make -C ../3LA-GlitchClose-Viewer install-desktop # optional: add to the app launcher
```

It has no shader of its own: `sync.py` extracts the GLSL from `shader.hpp` and
the option list — keys, defaults, min/max — from `main.cpp`, and the viewer's
`make` re-runs it whenever either changes. See
[its README](../3LA-GlitchClose-Viewer/README.md).

## How it renders

The shader never draws to the screen directly. At `RENDER_POST_WINDOWS`:

1. the shader runs over a full-viewport quad into the effect's own
   framebuffer, using identity NDC coordinates, so no projection matrix is
   involved
2. that framebuffer is handed to an ordinary `CTexPassElement`, so Hyprland
   keeps owning positioning, clipping, damage and colour management

The window snapshot comes from `makeSnapshotFB()`, which may return a
monitor-sized framebuffer. That framebuffer is in the monitor's *logical*
orientation (`m_transformedSize`; the rotation is only applied when the
finished frame is blitted to the output), so the window sits upright inside it
even on a rotated (portrait) monitor. `buildUV()` maps the shader's local 0..1
uv straight onto the window's box in the snapshot — a scale + offset, passed as
the `uvOffset` / `uvXf` uniforms (`uvXf` is a full `mat2`, so any affine map
fits). A window-sized snapshot gets the identity map. The shader's own output
is logical too, so the composite needs no transform.

Program binding goes through `g_pHyprOpenGL->useShader()` rather than raw
`glUseProgram()`: `CHyprOpenGLImpl` caches the bound program, and desyncing
that cache breaks the *next* Hyprland draw. Viewport and blend go through the
renderer wrappers for the same reason.

## Notes

- **The border is never glitched.** The effect box is the window's content box
  inset by the border width, so the border ring stays intact and a
  border-wide strip of content along each edge is left untouched.
- Skipped, with no effect (`close()` closes them straight away): everything
  while `enabled = 0`, X11 override-redirect windows, child windows
  (`ignore_children`), `ignore_class` / `ignore_title` matches, windows under
  `min_size` on either axis, and a window that already has an effect playing.
- The overlay is drawn only on the window's own monitor, and only while its
  workspace (or special workspace) is the one shown there.
- `ignore_class` / `ignore_title` are C++ `std::regex` (ECMAScript, matched
  anywhere in the string), **not** Lua patterns. An invalid pattern disables
  that filter.
- The shader is compiled lazily on the first frame of the first effect. A
  compile failure logs, raises one notification, and latches off: nothing is
  drawn after that, but `close()` still waits `close_at` × `duration` before
  closing and the close animation stays suppressed.
- Unloading the plugin sends any pending close immediately and gives windows
  under a playing effect their animations back.
- Each effect logs `glitch on class=… title=…` at `DEBUG`; some skips (not
  mapped, override-redirect, effect already playing) and every `window.close`
  received log at `TRACE`. Logs need `debug:disable_logs = false`; `TRACE`
  also needs Hyprland started with `HYPRLAND_TRACE=1`.
