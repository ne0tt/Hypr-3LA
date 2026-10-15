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

The whole visual is one GLSL fragment shader — the window snapshot goes
through a single pass into the effect's own framebuffer, which is then
composited. So:

- displacement is **continuous and per-pixel**
- the v-sync frame tear is real: the content on each side of the seam is
  evaluated at a different animation step
- chromatic aberration is a genuine per-channel UV offset
- static is sub-pixel and never repeats
- one draw call per frame
- every knob is a live uniform, so re-running matugen re-themes a running
  effect without reloading the plugin

Part of [Hypr-3LA](../README.md).

## Contents

- [Build and load](#build-and-load)
- [The `glitchclose:close` dispatcher](#the-glitchcloseclose-dispatcher)
- [Config](#config)
- [Tuning it](#tuning-it)
- [How it renders](#how-it-renders)
- [Notes](#notes)

## Build and load

Requires Hyprland 0.56.2 headers (see the [main README](../README.md#requirements))
and the **GL renderer** — on a Vulkan backend it raises a notification and
disables itself.

```sh
make                                              # → 3LA-GlitchClose.so
hyprctl plugin load "$PWD/3LA-GlitchClose.so"     # absolute path required
hyprctl getoption plugin:3la_glitch_close:duration
```

Or via hyprpm: `hyprpm enable 3LA-GlitchClose`. Rebuild after every Hyprland
update.

## The `glitchclose:close` dispatcher

**Use this to close windows.** Closing first and animating afterwards means
the layout has already re-tiled and the effect plays over the neighbours that
took the space. The dispatcher fixes the ordering: the collapse plays over the
still-open window, and the real close goes out at `close_at` × `duration` — by
default on the frame the burst ends.

```lua
-- falls through to a plain close when the plugin isn't loaded
hl.bind("SUPER + Q", function()
    if pcall(function() hl.plugin.glitchclose.close() end) then return end
    hl.dispatch(hl.dsp.window.close())
end, { description = "Close active window (glitch collapse)" })
```

Classic configs: `bind = $mainMod, Q, glitchclose:close`.

`hyprctl dispatch glitchclose:close` does **not** work under the Lua config
parser (it parses the argument as Lua). Use
`hyprctl eval 'hl.plugin.glitchclose.close()'` instead.

**It always ends on a glitched window.** After the burst the overlay *holds*
at full collapse until the window is really gone, and only then runs the
`fade` tail — fading earlier would dissolve back onto the untouched window
still waiting to unmap. Two things make the hold work:

- the window's **own close animation is suppressed** for the effect's
  duration; otherwise Hyprland's fade-out snapshot looks like the window
  reappearing.
- `hold` caps the wait (default 1000 ms). An app that refuses to close — an
  unsaved-changes dialog — gets the fade anyway and its animations back.

The tile slot is held for the whole burst. Set `close_at` below 1 to send the
close mid-burst instead.

### Windows closed some other way

Windows closed by something else (an external `killactive`, an app responding
to a close request) still get a post-hoc effect via the `window.close` event.
That fires at *unmap*, so the snapshot usually fails and the shader falls back
to its static-only mode. An application that **exits on its own** never emits
`window.close`, so it gets no effect.

**Workaround for shell exits:** shadow `exit` with a function that calls the
dispatcher before the shell terminates, falling back to a real exit when the
plugin isn't loaded:

```zsh
function exit(){
    if hyprctl dispatch 'assert(false, type(hl.plugin) .. "/" .. type((hl.plugin or {}).glitchclose))' 2>&1 \
         | grep -q 'table/table'; then
        hyprctl dispatch '(function() hl.plugin.glitchclose.close() return hl.dsp.no_op() end)()'
    else
        builtin exit "$@"
    fi
}
```

The probe deliberately fails an `assert` so its message comes back on stderr —
`hyprctl dispatch` has no other way to return a value. The `return
hl.dsp.no_op()` exists because `close()` returns nothing, which `hyprctl
dispatch` would treat as an error. `close()` acts on the *focused* window, so
this assumes the terminal running `exit` is focused.

## Config

Defaults shown:

```lua
pcall(hl.config, {
    plugin = {
        ["3la_glitch_close"] = {
            enabled = 1,           -- 0 = off: windows close normally with no
                                   -- effect; the dispatcher still closes
            duration = 700,        -- collapse duration (ms)
            fade = 80,             -- fade-out tail (ms), started only once the
                                   -- window is actually gone
            hold = 1000,           -- max ms to hold the finished collapse while
                                   -- waiting for the window to vanish
            close_at = 1.0,        -- when glitchclose:close sends the real close,
                                   -- as a fraction of `duration`

            strength = 1.0,        -- master multiplier on every displacement
                                   -- term (0 = calm, up to 5 = extreme)
            aberration = 0.5,      -- RGB channel split
            blocks = 0.6,          -- slice tearing + macroblock corruption
            noise = 0.5,           -- digital static
            scanlines = 0.4,       -- CRT scanline darkening
            roll = 0.5,            -- rolling bright bar sweeping the window
            melt = 0.5,            -- wavy vertical tear boundary; 0 = flat shear
            tear = 0.5,            -- v-sync frame tear (0 = off)
            tear_speed = 3.0,      -- seam sweeps per burst
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
            text_size = 16,        -- pt
            text_alpha = 1.0,      -- 0 = plate with no text
            text_at = 0.4,         -- fraction of duration before it appears
            text_blink = 0,        -- blink half-period in ms (0 = steady)
            text_padding = 14,     -- gap between text and plate edge (px)
            text_bg_round = 4,     -- plate corner radius (px)
            text_bg_alpha = 0.85,  -- 0 = text with no plate
            ["col.text"] = 0,      -- 0 = white
            ["col.text_bg"] = 0,   -- 0 = red

            min_size = 80,         -- skip windows smaller than this (px)
            ignore_children = 1,   -- skip dialogs, transients, modals
            ignore_class = "^(xdg-desktop-portal.*)$",
            ignore_title = "",
        }
    }
})
```

### What the dials do

- `strength` scales *all* displacement; the individual weights are 0..1 dials
  on one shader stage each, so `strength = 2, blocks = 0` gives heavy melt and
  aberration with no slice displacement.
- `ghost` echoes the **whole frame** sideways at low alpha, added rather than
  blended so overlaps brighten — text comes out doubled. It is what makes a
  torn frame read as a *doubled signal*. Set `0` for clean shearing.
- `blocks` vs `tear`: `blocks` scatters many short-lived strips at random
  offsets. `tear` is a single coherent **v-sync seam** sweeping down the
  window, with everything below it shifted and evaluated one animation step
  out of date, so it reads as a frame boundary. `tear_speed` sets how many
  times the seam crosses; with a short `duration`, 1–2 reads as a glitch and
  higher as a strobe.
- The caption is composited on top, drawn through the burst and hold but
  never the fade. `text_at` is a fraction of `duration` — with a short
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

Colours are read as uniforms every frame, so re-running matugen re-themes a
live effect with no plugin reload.

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
the option list — keys, defaults, min/max — from `main.cpp`. Re-run it after
touching either. See [its README](../3LA-GlitchClose-Viewer/README.md).

## How it renders

Plugins cannot reach Hyprland's projection matrix (`monitorProjection` is
private to `CHyprOpenGLImpl`), so the shader never draws directly to the
screen. Instead, at `RENDER_POST_WINDOWS`:

1. the shader runs over a full-viewport quad into the effect's own
   framebuffer, using identity NDC coordinates
2. that framebuffer is handed to an ordinary `CTexPassElement`, so Hyprland
   keeps owning positioning, clipping, damage and colour management

Because `makeSnapshotFB()` may return a monitor-sized framebuffer, the
`uvOffset` / `uvXf` uniforms map the window's sub-rect onto 0..1. `uvXf` is a
full `mat2` rather than a scale because the snapshot is rendered through the
monitor's own projection: on a rotated (portrait) monitor the window's pixels
sit rotated inside it, and carrying that rotation keeps the tearing running
across the window instead of down it. For the same reason the composite does
*not* set `flipEndFrame`.

Program binding goes through `g_pHyprOpenGL->useShader()` rather than raw
`glUseProgram()`: `CHyprOpenGLImpl` caches the bound program, and desyncing
that cache breaks the *next* Hyprland draw. Viewport and blend go through the
renderer wrappers for the same reason.

## Notes

- **The border is never glitched.** The effect box is inset by the window's
  border width, so the border ring stays intact while the content tears
  apart. The outermost 1 px of window content is left untouched.
- The shader is compiled lazily on the first frame of the first effect. A
  compile failure logs, raises one notification, and latches off — closes
  then behave like a plain `killactive`.
- `ignore_class` / `ignore_title` are C++ `std::regex`, **not** Lua patterns.
- Skip reasons are logged at `TRACE` (enable with `debug:enable_trace`) —
  useful when a close silently produces no effect.
