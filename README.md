# Hypr-3LA

Two Hyprland plugins that give a tiling desktop a CCTV / surveillance-rig
aesthetic: **[3LA-Corners](#3la-corners)** frames every window with
targeting-reticle corner brackets, and **[3LA-GlitchClose](#3la-glitchclose)**
kills windows with a GLSL "signal lost" glitch collapse instead of letting them
blink out.

Both are C++ Hyprland plugins built against **Hyprland 0.56.2**, configured
either through classic `hyprland.conf` keywords or Hyprland's Lua config
(`hl.config`), and installable with `hyprpm`.

```
3LA-Corners/              corner brackets decoration
3LA-GlitchClose/          GLSL shader signal-loss collapse
3LA-GlitchClose-Viewer/   WebGL tuner for the shader above
hyprpm.toml               plugin manifest + Hyprland/plugin commit pins
```

Each plugin directory is self-contained: `make` there produces the `.so`, and
`autoload.sh` is a startup fallback that loads it via `hyprctl` and re-applies
its settings.

## Demo

**3LA-GlitchClose** — three kitty terminals framed by 3LA-Corners, closed one by
one through `glitchclose:close`. Setup runs at 2×; the closes are real time:

![demo: three terminals closed with the shader glitch collapse](assets/glitchclose-demo.gif)

([full-quality mp4](assets/glitchclose-demo.mp4))

Every frame of the collapse is one fragment-shader pass: v-sync seam, slice
tearing, macroblock corruption, whole-frame ghost copies, chromatic aberration
and static, with the caption composited on top so it stays legible. The window's
own border stays clean throughout — the effect is inset strictly inside it.

## Install

Via hyprpm (uses `hyprpm.toml` in this repo):

```sh
hyprpm add https://github.com/ne0tt/Hypr-3LA
hyprpm enable 3LA-Corners
hyprpm enable 3LA-GlitchClose
```

Or build and load manually:

```sh
make -C 3LA-Corners && make -C 3LA-GlitchClose   # or `make debug` for -g symbols
hyprctl plugin load "$PWD/3LA-Corners/3LA-Corners.so"
hyprctl plugin load "$PWD/3LA-GlitchClose/3LA-GlitchClose.so"
```

Autoload from `hyprland.lua`:

```lua
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-Corners/3LA-Corners.so")
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-GlitchClose/3LA-GlitchClose.so")
```

Then apply settings and reload without restarting Hyprland:

```sh
hyprctl eval 'dofile(os.getenv("HOME") .. "/.config/hypr/config/plugins.lua")'
```

### Requirements

- the `hyprland` package's headers (`/usr/include/hyprland`) and `pkg-config`
- a C++26 compiler (`g++`); the plugins link nothing — every symbol, including
  the GL entry points, resolves against the running compositor at `dlopen`
- a GL renderer for 3LA-GlitchClose; it disables itself on a Vulkan backend

**Plugins must be rebuilt whenever Hyprland updates.** The API commit hash is
checked at load time and a mismatch refuses to load with a notification rather
than crashing. Release builds are stripped — `make debug` keeps `-g` symbols for
backtraces, and the dynamic exports Hyprland needs survive stripping either way.

If you are installing via `hyprpm`, the `commit_pins` entry in `hyprpm.toml`
pairs a Hyprland commit with a plugin-repo commit; bump the second hash after
committing, or `hyprpm update` will keep fetching the older revision.

### Tuning GlitchClose

The shader has a lot of dials. Rather than closing a window every time you want
to see what one does, use the bundled WebGL tuner — it runs the plugin's actual
shader over a still image, with sliders and a scrubbable timeline:

```sh
make -C 3LA-GlitchClose-Viewer install-desktop   # optional: add to the app launcher
./3LA-GlitchClose-Viewer/run.sh
```

It has a **copy Lua** button that emits a ready-to-paste `pcall(hl.config, …)`
block. See [3LA-GlitchClose-Viewer/README.md](3LA-GlitchClose-Viewer/README.md).

## Configuration

Everything in this README uses Hyprland's Lua config: settings are applied
with `hl.config`, wrapped in `pcall` at startup so applying config for a
not-yet-loaded plugin can't break the rest of the config:

```lua
pcall(hl.config, { plugin = {
    ["3la_corners"]      = { offset = 10, length = 40, thickness = 1 },
    ["3la_glitch_close"] = { duration = 700, strength = 1.0, text = "SIGNAL LOST" },
} })
```

(The classic `hyprland.conf` ini keywords work too, with the same option
names: `plugin { 3la_glitch_close { duration = 700 } }`.)

Live-tweak any option without reloading (`hyprctl keyword` does not work with
the Lua parser — use `hl.config`):

```sh
hyprctl eval 'hl.config({ plugin = { ["3la_glitch_close"] = { strength = 2.0 } } })'
hyprctl getoption plugin:3la_glitch_close:duration   # inspect
```

The demo above uses matugen-generated colors — teal static and caption on a
theme backdrop — passed straight from the generated Lua color globals into the
`col.*` options.

## Reference setup (Lua config)

The exact wiring from a working config (`~/.config/hypr`):

**`hyprland.lua`** — register the plugins at startup, with an `autoload.sh`
fallback that loads via `hyprctl` and re-applies settings if the startup
registration doesn't take:

```lua
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-Corners/3LA-Corners.so")
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-GlitchClose/3LA-GlitchClose.so")

hl.on("hyprland.start", function()
    hl.exec_cmd(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-Corners/autoload.sh")
    hl.exec_cmd(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-GlitchClose/autoload.sh")
end)

require("config.colors")  -- matugen globals, before anything that uses them
require("config.plugins") -- plugin settings (see below)
```

**`config/plugins.lua`** — the settings, wrapped in `pcall` so applying config
for a not-yet-loaded plugin can't break the rest of the startup:

```lua
pcall(hl.config, {
    plugin = {
        ["3la_corners"] = {
            offset = 10, length = 40, thickness = 1,
            ["col.active"] = primary,   -- matugen color globals
            ["col.inactive"] = primary, -- same color: no focus-based change
            flash_count = 3, flash_duration = 200,
            flash_on_focus = 1, focus_flash_count = 2, focus_flash_duration = 75,
            glow = 1, ["glow.size"] = 10, ["glow.strength"] = 0.4,
            ["col.glow"] = 0,           -- follow the bracket color
        }
    }
})

pcall(hl.config, {
    plugin = {
        ["3la_glitch_close"] = {
            duration = 350, fade = 20, close_at = 1.0,
            strength = 0.38, aberration = 0.56, blocks = 0.15, noise = 0.00,
            scanlines = 0.37, roll = 0.08, melt = 0.10, vignette = 0.00,
            tear = 0.55, tear_speed = 2.0, ghost = 0.6,
            backdrop_alpha = 0.3,
            ["col.backdrop"] = on_secondary,   -- matugen color globals
            ["col.fringe1"] = on_error,
            ["col.fringe2"] = primary,

            text = "SIGNAL LOST", text_size = 16, text_at = 0.25,
            text_padding = 14, text_bg_round = 4, text_bg_alpha = 0.85,
            ["col.text"] = 0,      -- 0 = white
            ["col.text_bg"] = 0,   -- 0 = red
        }
    }
})
```

Re-running matugen re-themes a live effect: every colour is read as a shader
uniform each frame, so no plugin reload is needed.

Note that `hl.config` reports `unknown config key` **per key** for a plugin that
is not loaded, and does so *without raising* — the surrounding `pcall` does not
swallow it. Comment a plugin's settings block out at the same time as its
`hl.plugin.load` line, or every reload prints one error per option.

**`config/keyboard/keybindings.lua`** — the kill key, falling through to a plain
close when the plugin is not loaded (`hl.plugin.glitchclose` only exists while
it is):

```lua
hl.bind(mainMod .. " + Q", function()
    if pcall(function() hl.plugin.glitchclose.close() end) then return end
    hl.dispatch(hl.dsp.window.close())
end, { description = "Close active window (glitch collapse)" })
```

Bind the tuner too, if you are iterating on the shader:

```lua
hl.bind(mainMod .. " + SHIFT + G",
    hl.dsp.exec_cmd(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-GlitchClose-Viewer/run.sh"),
    { description = "GlitchClose shader tuner" })
```

---

# 3LA-Corners

Draws decorative corner brackets outside every window's border. Each corner
gets two line segments (horizontal + vertical) offset outside the window
border, like a targeting reticle. The focused window and unfocused windows can
be given different colors, and either window spawn or focus gain can trigger a
flash.

## Config

Defaults shown:

```lua
hl.config({ plugin = { ["3la_corners"] = {
    offset = 10,          -- gap (px) between the window border and the brackets
    length = 100,         -- arm length (px) of each bracket, from the corner
    thickness = 2,        -- line thickness (px)

    ["col.active"] = 0,   -- focused-window bracket color.
                          -- 0 = follow general:col.active_border (first color);
                          -- otherwise e.g. "rgba(33ccffee)"
    ["col.inactive"] = 0, -- unfocused-window bracket color.
                          -- 0 = follow col.active if that is set, else
                          -- general:col.inactive_border (first color)

    flash_count = 3,      -- times the brackets flash when a window SPAWNS (0 = off)
    flash_duration = 150, -- spawn flash on/off phase duration (ms)

    flash_on_focus = 0,         -- flash when a window gains FOCUS (0 = off, 1 = on)
    focus_flash_count = 3,      -- times the brackets flash per focus change (0 = off)
    focus_flash_duration = 150, -- focus flash on/off phase duration (ms)

    glow = 0,             -- soft halo behind the brackets DURING a flash burst
                          -- (0 = off). See "Glow" below: it is not drawn on
                          -- every frame, only while a burst is running.
    ["glow.size"] = 12,   -- halo spread distance (px)
    ["glow.strength"] = 0.5, -- overall halo intensity (0..1)
    ["col.glow"] = 0,     -- 0 = follow the bracket's own color
} } })
```

### Flashing

A burst is `count` on-pulses, each phase lasting `duration` ms, after which the
brackets settle to steady on. The two burst types are configured independently:

| Trigger | Count | Duration | Gate | Priority |
|---|---|---|---|---|
| window spawns | `flash_count` | `flash_duration` | always on | preempts any running burst |
| window gains focus | `focus_flash_count` | `focus_flash_duration` | `flash_on_focus = 1` | dropped while a spawn burst runs |

The focus flash needs **both** `flash_on_focus = 1` and a non-zero
`focus_flash_count`; setting either to `0` disables it. `flash_count = 0`
disables the spawn flash only — the two are configured independently.

**The spawn burst has priority.** A focus flash cannot preempt a spawn burst
that is still running; it is dropped, and the spawn burst plays to completion.
This matters because a new window takes focus in the same tick it opens, so
without the priority rule the focus burst would replace every spawn burst and
`flash_count` / `flash_duration` would have no visible effect. A spawn burst
preempts anything, including a running focus burst.

Burst parameters are captured when the burst is armed, so changing the options
mid-burst does not retime the burst already in flight. Re-triggering a focus
flash mid-burst restarts the sequence rather than queueing, so rapid alt-tabbing
stays in step with the focus rather than lagging behind it.

Burst expiry is time-based, computed from the instant the burst was armed rather
than from frames drawn. A burst therefore expires on schedule even while nothing
is being rendered — a window that spawns fullscreen (brackets hidden) does not
get stuck mid-burst or suppress its next flash.

### Glow

A soft halo behind the brackets, off by default (`glow = 0`).

**It only renders while a spawn or focus flash burst is actively running** — not
on every frame. The brackets sit plain the rest of the time, so the glow reads as
part of the flash rather than as a permanent style. That also means `glow = 1`
does nothing visible if both flash types are disabled.

It is **not a real blur.** A plugin cannot reach Hyprland's shadow shader, so
this is four expanded copies of each bracket box drawn behind it with fading
alpha — a stepped halo rather than a smooth one. `glow.size` sets how far the
outermost layer expands, `glow.strength` scales the whole stack. At large sizes
the banding between layers becomes visible; it reads best as a tight halo
(≤ ~16px) rather than a wide bloom.

`col.glow = 0` follows the bracket's own colour, including its flash alpha, so
the halo fades in and out with the pulse. Set it explicitly to tint the halo
differently from the brackets.

The decoration's damage region grows by `glow.size` whenever `glow` is on,
because the halo extends past the bracket boxes; without that its outer edge
leaves trails while a window is moved or resized.

### Active / inactive styling

The brackets render at **full alpha regardless of focus**, fully decoupled from
window opacity. The `a` alpha the renderer passes to the decoration is ignored
outright, so none of these affect the brackets:

- `decoration:active_opacity` / `decoration:inactive_opacity`
- per-window `opacity` window rules
- window fade-in/out and workspace-move alpha

Only two things set the final alpha: the alpha channel of the configured color
(`col.active` / `col.inactive`) and the flash envelope. Trade-off: brackets pop
in at full opacity when a window opens rather than fading with it.

> **If unfocused brackets look "dimmed", check `col.inactive` before suspecting
> opacity.** With `col.inactive = 0` the brackets follow
> `general:col.inactive_border`, which in most themes is a much darker shade than
> `col.active_border` — that reads as dimming but is pure color. Set both colors
> explicitly to the same value for identical brackets in both states.

Color resolution order:

| State | Order |
|---|---|
| focused | `col.active` -> `general:col.active_border` -> white |
| unfocused | `col.inactive` -> `col.active` -> `general:col.inactive_border` -> white |

The unfocused fallback to `col.active` means setting only `col.active` gives every
window the same bracket color. Distinct colors per state:

```lua
hl.config({ plugin = { ["3la_corners"] = {
  ["col.active"] = "rgba(33ccffee)", ["col.inactive"] = "rgba(6a7a85ff)",
} } })
```

## Notes

- The bracket ring is *reserved* space: tiled windows shrink by
  `offset + thickness` per side so brackets never overlap neighbors.
- Brackets are hidden on fullscreen windows. A burst armed on such a window still
  expires on its own schedule, since expiry is time-based rather than frame-driven.
- **Breaking:** the old `color` option was renamed to `col.active`. `color` is no
  longer registered and Hyprland will report it as an unknown option — rename it
  in your config.
- Active state is read per-frame from `Desktop::focusState()->isWindowActive()`,
  and the plugin listens on the `window.active` event to damage both the window
  losing focus and the one gaining it, so brackets repaint immediately on focus
  change. `flash_on_focus` reuses that same listener, subject to the spawn-burst
  priority rule above.
- While a flash is running the decoration damages itself every frame to drive the
  animation, so `flash_on_focus = 1` costs a short burst of redraws per focus
  change. On a heavily loaded GPU prefer a low `focus_flash_count`.
- **Removed:** `active_opacity` / `inactive_opacity` no longer exist. They fought
  with the compositor's own opacity handling; brackets are now always full alpha.
  Remove them from your config or Hyprland will report unknown options.

---

# 3LA-GlitchClose

Plays a CCTV "signal lost" collapse over a window when it closes: the window's
own content tears apart, static and a backdrop ramp in over it, a `SIGNAL LOST`
caption appears, and only then does the window actually close.

The whole visual is a single GLSL fragment shader. The window snapshot goes
through one shader pass into the effect's own framebuffer, which is then
composited — so:

- displacement is **continuous and per-pixel**, not quantised to slice quads
- a real v-sync frame tear: the seam needs the content on each side evaluated at
  a different animation step, which stacked quads cannot express
- chromatic aberration is a real per-channel UV offset, not a tinted overlay
  rectangle
- static is sub-pixel and never repeats
- one draw call per frame
- every knob is a live uniform, so re-running matugen re-themes the effect
  without reloading the plugin

## How it renders

Plugins cannot reach Hyprland's projection matrix (`monitorProjection` is
private to `CHyprOpenGLImpl`), so the shader never draws directly to the screen.
Instead, at `RENDER_POST_WINDOWS`:

1. the shader runs over a full-viewport quad into the effect's own framebuffer,
   using identity NDC coordinates — no matrix maths needed
2. that framebuffer is handed to an ordinary `CTexPassElement`, so Hyprland
   keeps owning positioning, clipping, damage and colour management

Because `makeSnapshotFB()` may return a monitor-sized framebuffer rather than a
window-sized one, the `uvOffset` / `uvXf` uniforms map the window's sub-rect onto
0..1 — the glitch geometry stays window-local either way. `uvXf` is a full `mat2`
rather than a scale because that snapshot is rendered through the monitor's own
projection: on a rotated (portrait) monitor the window's pixels sit rotated
inside it, and carrying that rotation in the mapping is what keeps the tearing
running across the window instead of down it. For the same reason the composite
does *not* set `flipEndFrame` — that flag composes the monitor transform's
inverse into the texture transform, which is right for a snapshot FB but would
rotate the finished effect a second time.

Program binding goes through `g_pHyprOpenGL->useShader()` rather than raw
`glUseProgram()`: `CHyprOpenGLImpl` caches the bound program to skip redundant
binds, and desyncing that cache breaks the *next* Hyprland draw. Viewport and
blend go through the renderer wrappers for the same reason.

## The `glitchclose:close` dispatcher (use this to close windows)

Closing first and animating afterwards means the layout has already re-tiled and
the effect plays over the neighbours that took the space.
The dispatcher fixes the ordering: the collapse plays over the still-open
window, and the real close request goes out at `close_at` × `duration` — by
default on the frame the burst ends.

**It always ends on a glitched window.** After the burst the overlay *holds* at
full collapse until the window is really gone, and only then runs the `fade`
tail. Fading earlier would dissolve the glitch back onto the untouched window
that is still sitting there waiting for the app to unmap — the one thing a
glitch close must not end on. Two things make the hold sufficient:

- the window's **own close animation is suppressed** for the duration of the
  effect (`noAnim` at `setprop` priority, the same slot `hyprctl setprop` uses).
  Otherwise Hyprland plays its fade-out snapshot of the real window just as the
  overlay ends, which looks exactly like the window reappearing.
- `hold` caps the wait (default 1000 ms). An application that refuses to close —
  an unsaved-changes dialog — gets the fade anyway and its animations back,
  rather than pinning the glitch on screen.

The tile slot is still held for the whole burst, so nothing is drawn over the
neighbours until the close actually goes out; only the hold and the fade can
overlap the re-tile, and by then the collapse is at full opacity. Set `close_at`
below 1 to send the close mid-burst instead.

```lua
hl.bind("SUPER + Q", function() hl.plugin.glitchclose.close() end,
    { description = "Close active window (glitch collapse)" })
```

(Classic configs: `bind = $mainMod, Q, glitchclose:close`.)

Note that `hyprctl dispatch glitchclose:close` does **not** work under the Lua
config parser — it parses the argument as Lua. Use
`hyprctl eval 'hl.plugin.glitchclose.close()'` instead.

Windows closed by something else (an external `killactive`, an app responding to
a close request) still get a post-hoc effect, driven by the `window.close`
event. That event fires at *unmap*, so the snapshot capture usually fails on
this path and the shader falls back to its static-only mode (`hasTex = 0`).
An application that simply **exits on its own** never emits `window.close`
at all, so it gets no effect — this is inherent to the event.

## Config

Defaults shown:

```lua
pcall(hl.config, {
    plugin = {
        ["3la_glitch_close"] = {
            duration = 700,        -- collapse duration (ms)
            fade = 80,             -- fade-out tail (ms), started only once the
                                   -- window is actually gone
            hold = 1000,           -- max ms to hold the finished collapse while
                                   -- waiting for the window to vanish, so the
                                   -- fade never dissolves back onto a live
                                   -- window
            close_at = 1.0,        -- when glitchclose:close sends the real close,
                                   -- as a fraction of `duration` (1 = as the
                                   -- burst ends; the hold then covers the
                                   -- app's close latency)

            strength = 1.0,        -- master multiplier on every displacement
                                   -- term (0 = calm, up to 5 = extreme)
            aberration = 0.5,      -- RGB channel split
            blocks = 0.6,          -- slice tearing + macroblock corruption
            noise = 0.5,           -- digital static
            scanlines = 0.4,       -- CRT scanline darkening
            roll = 0.5,            -- rolling bright bar sweeping the window
            melt = 0.5,            -- wavy vertical tear boundary; 0 = the torn
                                   -- edge shears flat instead of rippling
            tear = 0.5,            -- v-sync frame tear (0 = off)
            tear_speed = 3.0,      -- seam sweeps per burst
            ghost = 0.5,           -- whole-frame echo copies (0 = off)
            vignette = 0.4,        -- edge darkening as the feed collapses
            backdrop_alpha = 0.75, -- opacity the backdrop collapses to

            ["col.backdrop"] = 0,  -- 0 = black
            ["col.fringe1"] = 0,   -- 0 = magenta
            ["col.fringe2"] = 0,   -- 0 = cyan

            -- Caption drawn OVER the shader output (not fed through the
            -- glitch, so it stays legible). Burst only, never the fade tail.
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

`strength` scales *all* displacement; the individual weights are 0..1 dials on
one shader stage each, so `strength = 2, blocks = 0` gives heavy melt and
aberration with no slice displacement.

`ghost` echoes the **whole frame** sideways at low alpha, added rather than
blended so overlaps brighten — text and logos come out doubled. It is what makes
a torn frame read as a *doubled signal* rather than merely displaced strips, and
the most recognisable part of the look. Turn it down to `0` for clean shearing
with no echo.

`blocks` and `tear` are different things despite both being "tearing". `blocks`
scatters many short-lived horizontal strips at random offsets. `tear` is a single
coherent **v-sync seam** that sweeps down the window, with everything below it
shifted sideways as though that half of the frame arrived late — and evaluated
one animation step out of date, so the two sides of the seam carry different
slice, macroblock and grain patterns. That staleness is what makes it read as a
frame boundary rather than a plain horizontal offset. `tear_speed` sets how many
times the seam crosses during the burst; with a short `duration`, 1–2 reads as a
deliberate glitch and anything higher as a strobe.

The caption is composited on top of the shader rather than passed through it, so
it stays readable while the window behind it tears apart. It is drawn through the
burst and the hold but never the fade: a dissolving glitch still reads as an
effect, a legible caption dissolving over whatever is behind it reads as a bug.
Note `text_at` is a fraction of `duration` — with a short `duration` a default of
`0.4` leaves very little time on screen.

For matugen theming, pass the globals from your generated `colors.lua`:

```lua
["col.backdrop"] = background,
["col.fringe1"] = primary,
["col.fringe2"] = on_error,
```

These are read as uniforms every frame, so re-running matugen re-themes a live
effect with no plugin reload.

## Tuning it

[`3LA-GlitchClose-Viewer/`](3LA-GlitchClose-Viewer) is a WebGL2 tuner: scrub the
close animation over a still image, move the sliders, copy the result straight
into `plugins.lua` — rather than closing a terminal every time you want to see
what a value does.

```sh
make -C 3LA-GlitchClose-Viewer open
```

It has no shader of its own. `sync.py` extracts the GLSL from `shader.hpp` and
the option list — keys, defaults, min/max — from `main.cpp`, so the controls are
whatever the plugin currently registers, and a config key with no matching
uniform is a build error rather than a dead slider.

## Notes

- **The border is never glitched.** The effect box is inset by the window's
  border width, so no border pixel is fed through the shader and the border ring
  stays intact while the content tears apart. Without the inset the box sits
  flush against the border and edge sampling drags border colour inward, drawing
  a bright 1px frame around the effect. The cost is that the outermost 1px ring
  of window content is left untouched, which is not noticeable at typical border
  widths.
- The shader is compiled lazily on the first frame of the first effect (the GL
  context is only guaranteed current mid-render). A compile failure logs, raises
  one notification, and latches off — closes then behave like a plain
  `killactive` rather than retrying every frame.
- Requires the GL renderer; the effect disables itself on a Vulkan backend.
- `ignore_class` / `ignore_title` are C++ `std::regex`, **not** Lua patterns.
- Skip reasons are logged at `TRACE` (enable with `debug:enable_trace`) — useful
  when a close silently produces no effect.
