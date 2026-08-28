# Hypr-3LA

A pair of Hyprland plugins that give a tiling desktop a CCTV / surveillance-rig
aesthetic: **[3LA-Corners](#3la-corners)** frames every window with
targeting-reticle corner brackets, and **[3LA-Feed-Loss](#3la-feed-loss)**
kills windows with a "signal lost" static burst instead of letting them blink
out.

Both are C++ Hyprland plugins built against **Hyprland 0.56.2**, configured
either through classic `hyprland.conf` keywords or Hyprland's Lua config
(`hl.config`), and installable with `hyprpm`.

## Demo

Three kitty terminals framed by 3LA-Corners, closed one by one through
`feedloss:close`:

![demo: three terminals closed with the signal-lost burst](assets/feedloss-demo.gif)

([full-quality mp4](assets/feedloss-demo.mp4))

## Install

Via hyprpm (uses `hyprpm.toml` in this repo):

```sh
hyprpm add https://github.com/ne0tt/Hypr-3LA
hyprpm enable 3LA-Corners
hyprpm enable 3LA-Feed-Loss
```

Or build and load manually:

```sh
make -C 3LA-Corners && make -C 3LA-Feed-Loss     # or `make debug` for -g symbols
hyprctl plugin load "$PWD/3LA-Corners/3LA-Corners.so"
hyprctl plugin load "$PWD/3LA-Feed-Loss/3LA-Feed-Loss.so"
```

Autoload from `hyprland.lua`:

```lua
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-Corners/3LA-Corners.so")
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-Feed-Loss/3LA-Feed-Loss.so")
```

Building requires the `hyprland` package's headers (`/usr/include/hyprland`)
and `pkg-config`. Plugins must be rebuilt whenever Hyprland updates — the API
commit hash is checked at load time and a mismatch refuses to load. Release
builds are stripped (`make debug` keeps `-g` symbols for backtraces; the
dynamic exports Hyprland needs survive stripping either way).

## Configuration

Everything in this README uses Hyprland's Lua config: settings are applied
with `hl.config`, wrapped in `pcall` at startup so applying config for a
not-yet-loaded plugin can't break the rest of the config:

```lua
pcall(hl.config, { plugin = {
    ["3la_corners"]   = { offset = 10, length = 40, thickness = 1 },
    ["3la_feed_loss"] = { duration = 900, glitch = 3, text = "SIGNAL LOST" },
} })
```

(The classic `hyprland.conf` ini keywords work too, with the same option
names: `plugin { 3la_feed_loss { duration = 900 } }`.)

Live-tweak any option without reloading (`hyprctl keyword` does not work with
the Lua parser — use `hl.config`):

```sh
hyprctl eval 'hl.config({ plugin = { ["3la_feed_loss"] = { glitch = 5 } } })'
hyprctl getoption plugin:3la_feed_loss:duration   # inspect
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
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-Feed-Loss/3LA-Feed-Loss.so")

hl.on("hyprland.start", function()
    hl.exec_cmd(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-Corners/autoload.sh")
    hl.exec_cmd(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-Feed-Loss/autoload.sh")
end)

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
        }
    }
})

pcall(hl.config, {
    plugin = {
        ["3la_feed_loss"] = {
            duration = 500, close_at = 1.0, fade = 200,
            static_alpha = 0.10, backdrop_alpha = 0.20, glitch = 4,
            text = "SIGNAL LOST", text_size = 32, text_alpha = 0.5, text_blink = 0,
            ["col.text"] = primary, ["col.static"] = on_primary,
            ["col.backdrop"] = background,
            ["col.fringe1"] = primary, ["col.fringe2"] = on_error,
        }
    }
})
```

**`config/keyboard/keybindings.lua`** — the kill key, falling back to a plain
close when the plugin isn't loaded (`hl.plugin.feedloss` only exists while it
is):

```lua
hl.bind(mainMod .. " + Q", function()
    if not pcall(function() hl.plugin.feedloss.close() end) then
        hl.dispatch(hl.dsp.window.close())
    end
end, { description = "Close active window (feed loss)" })
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

# 3LA-Feed-Loss

Plays a CCTV "signal lost" burst over a window when it closes: the window's own
content tears apart, scanlined static and a backdrop ramp in over it, a
`SIGNAL LOST` caption appears, and only then does the window actually close —
the overlay dissolving in a short fade while the layout re-tiles underneath.

## Timeline

One burst is `duration` ms plus a `fade` ms tail (defaults 900 + 300):

1. **Glitch** (whole burst): the window's *own content* breaks up — a
   framebuffer snapshot is torn into horizontally displaced slices, with
   whole-frame jitter, drifting ghost copies and magenta/cyan color fringe on
   some slices. Escalates over time and never ramps out: the burst ends
   mid-glitch.
2. **Collapse** (~40–60%): animated scanlined static ramps in over the still
   glitching picture, then a backdrop; stray noise bands tear across.
3. **Caption** (from ~58% to burst end): the blinking `SIGNAL LOST` caption,
   centered. Burst only — it never lingers into the fade over re-tiled
   neighbors.
4. **Close + fade**: the real close goes out at `close_at` × `duration`
   (default: the very end of the burst), then the overlay fades out over
   `fade` ms while the layout re-tiles underneath.

The overlay stays **inside the window's border**, so the border ring (and the
Corners brackets outside it) keeps framing the static until the window
actually closes.

Post-hoc closes (window died on its own — no snapshot possible) skip the
content tearing and cut straight to static, backdrop at ~8% and caption at
~16% of the burst.

## The `feedloss:close` dispatcher (use this to close windows)

If the burst only starts *after* a window is gone, the layout has already
re-tiled and the static plays over the neighbors that took the space. The
dispatcher fixes the ordering: it plays the burst over the **still-open**
window — its tile slot stays occupied, nothing moves — and sends the real
close request at `close_at` × `duration` (default 100%: only once the burst
is over and the caption is gone). The re-tile then happens under the fading,
caption-free static of the fade tail.

Bind your kill key to it instead of `killactive` — the plugin registers a Lua
function, `hl.plugin.feedloss.close()` (see
[Reference setup](#reference-setup-lua-config) for a bind with a fallback):

```lua
hl.bind("SUPER + Q", function() hl.plugin.feedloss.close() end,
    { description = "Close active window (feed loss)" })
```

(Classic configs can use the dispatcher directly:
`bind = $mainMod, Q, feedloss:close`.)

Like `killactive` it *requests* the close, so apps can still show "unsaved
changes" dialogs — the feed comes back if the app refuses to die. Windows that
close on their own (app exits, `killactive` from elsewhere) still get a
post-hoc burst. A `feedloss:close` on a window the effect would skip (too
small, override-redirect, filtered) just closes it normally — the bind never
silently no-ops.

## Config

Defaults shown:

```lua
hl.config({ plugin = { ["3la_feed_loss"] = {
    duration = 900,        -- burst duration (ms); overlay at full strength throughout
    fade = 300,            -- overlay fade-out tail (ms) appended after the burst
    close_at = 1.0,        -- feedloss:close sends the real close at this
                           -- fraction of duration (0 = close immediately;
                           -- 1 = only after the burst, so the tile slot is
                           -- held until the fade tail)

    static_alpha = 0.55,   -- opacity of the animated static layer (0..1)
    backdrop_alpha = 0.75, -- opacity of the backdrop behind the static (0..1)
    glitch = 1,            -- glitch strength: 0 = off, 1 = normal, up to
                           -- 5 = extreme (scales tear amplitude and the
                           -- number of slices/bands)

    text = "SIGNAL LOST",  -- caption; "" = no caption
    text_size = 16,        -- caption font size (pt)
    text_alpha = 1.0,      -- caption opacity (0..1; 0 = hidden)
    text_blink = 1,        -- blink the caption (0 = steady)
    font = "monospace",    -- caption font family

    ["col.text"] = 0,      -- caption color (0 = white)
    ["col.static"] = 0,    -- static noise tint (0 = neutral gray); baked into
                           -- the noise textures, regenerated on config reload
    ["col.backdrop"] = 0,  -- backdrop color (0 = black); alpha comes from
                           -- backdrop_alpha, not the color
    ["col.fringe1"] = 0,   -- glitch-slice fringe color A (0 = magenta)
    ["col.fringe2"] = 0,   -- glitch-slice fringe color B (0 = cyan)

    min_size = 80,         -- skip windows smaller than this on either axis (px);
                           -- keeps menus/tooltips/popups from triggering bursts
    ignore_children = 1,   -- skip child windows — file open/save dialogs,
                           -- transients, modals (0 = burst on them too)

    -- Regex (C++ std::regex, NOT a Lua pattern) of window classes to never
    -- burst on; "" = filter off. The default catches portal file choosers
    -- (VS Code / Electron / Flatpak apps), which are separate processes and
    -- never look like children on Wayland.
    ignore_class = "^(xdg-desktop-portal.*)$",

    -- Same idea, matched against the window TITLE, for dialogs class alone
    -- can't identify, e.g. "^(Open File|Save As|Select Folder)$".
    -- "" (default) = filter off.
    ignore_title = "",
} } })
```

## Notes

- The effect is **screen-space**: it plays over the rectangle the window
  occupied when the burst started. Via `feedloss:close` that rectangle stays
  occupied by the window itself until `close_at`; for post-hoc bursts anything
  that moves into it (a neighbor re-tiling into the gap, a workspace slide)
  gets drawn over for the remaining duration.
- It composes with Hyprland's own close animation: the fade-out snapshot plays
  underneath the static. Setting the `fadeOut` animation faster (or off) makes
  the "feed cut" read harder; leaving it on reads more like corruption.
- The overlay renders at `RENDER_POST_WINDOWS`, i.e. above all windows but below
  top/overlay layers — bars and notifications stay on top of the static.
- If the workspace the window was on stops being the monitor's active (or
  active special) workspace mid-burst, drawing is suppressed; the burst still
  expires on its own schedule.
- X11 override-redirect surfaces and windows smaller than `min_size` don't
  trigger the effect, so context menus and tooltips stay quiet. With
  `ignore_children` on (default), child windows — xdg toplevels with a parent
  (including xdg-foreign), X11 transients/modals, xdg-dialog-v1 modals — are
  skipped too.
- Portal file choosers (`xdg-desktop-portal-gtk` etc.) are toplevels of a
  *separate process*; Electron apps like VS Code don't pass them a parent
  handle on Wayland, so they don't count as children. The `ignore_class`
  regex catches them by class instead (`ignore_title` exists for dialogs
  class can't identify).
- The class/title filters match against the window's *cached* class/title:
  the close event fires during unmap, when the xdg toplevel resource (what
  `fetchClass()` reads) can already be destroyed and would report `""`.
- Every burst start is logged at DEBUG level
  (`[3LA-Feed-Loss] burst on class='..' title='..'`) — check
  `hyprctl rollinglog` when a window bursts that you expected filtered.
- The glitch phase draws slices of a `makeSnapshotFB` capture taken when the
  effect starts, so it shows the window as it looked at the moment of "death" —
  everything is clipped to the window box, and slice geometry holds for 60 ms
  per step so it reads as tearing rather than shimmer. The snapshot framebuffer
  (potentially monitor-sized) lives only for the effect's duration.
- The static is 8 pre-baked 128×128 scanlined noise frames cycled every 45 ms
  and stretched over the window box — deliberately blocky, VHS-style. Textures
  are built lazily on first use and freed on unload.
- The caption texture is rendered once (at 2× size for hidpi sharpness) and
  cached; a config reload rebuilds it, so text/font/color changes apply live.
- While a burst runs its box is damaged every frame — cost is a sub-second
  full-redraw of that rectangle per closed window, nothing when idle.
