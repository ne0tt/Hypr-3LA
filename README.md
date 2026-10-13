# Hypr-3LA

Hyprland plugins that give a tiling desktop a CCTV / surveillance-rig
aesthetic: **[3LA-MilSpec](#3la-milspec)** wraps every window in a chamfered
HUD-panel frame (top-left and bottom-right corners cut at 45°), optionally with
a title bar inside it; **[3LA-Corners](#3la-corners)** frames every window with
targeting-reticle corner brackets; **[3LA-GlitchClose](#3la-glitchclose)**
kills windows with a GLSL "signal lost" collapse instead of letting them blink
out; **[3LA-TitleBars](#3la-titlebars)** reserves a solid-color bar above
the top of every window, with the window's title drawn on it; and
**[3LA-Tint](#3la-tint)** is for the apps that don't take ricing (they ignore
your GTK/Qt theme or have no theming at all): it recolours their greys, blacks
and whites toward your theme colour so they sit more comfortably with the rest
of the desktop.

All are C++ Hyprland plugins built against **Hyprland 0.56.2**, configured
either through classic `hyprland.conf` keywords or Hyprland's Lua config
(`hl.config`), and installable with `hyprpm`.

```
3LA-MilSpec/              chamfered mil-spec frame (+ optional title bar)
3LA-Corners/              corner brackets decoration
3LA-GlitchClose/          GLSL shader signal-loss collapse
3LA-GlitchClose-Viewer/   WebGL tuner for the shader above
3LA-TitleBars/            solid-color title bar overlay
3LA-Tint/                 GLSL recolour of a window's grey pixels
hyprpm.toml               plugin manifest + Hyprland/plugin commit pins
```

Each plugin directory is self-contained: `make` there produces the `.so`, and
`autoload.sh` is a startup fallback that loads it via `hyprctl` and re-applies
its settings.

## What's new

[![3LA-MilSpec: chamfered frame with title bar](assets/milspec.png)](#3la-milspec)

- **New plugin: [3LA-MilSpec](#3la-milspec)**, a chamfered HUD frame around
  every window, with:
  - an optional title bar inside the frame, inset per side
    (`titlebar.offset.*`), with separate active/inactive bar and text colours,
    active/inactive bar opacity (`titlebar.opacity.*`) and a
    [per-window toggle](#toggling-the-title-bar-per-window)
    (`3la_milspec:toggle` / `hl.plugin.milspec.toggle()`)
  - per-window opt-outs for the whole frame (`ignore_*`), just the bar
    (`titlebar.ignore_*`), just the stroke (`frame`, `frame.ignore_*`) and just
    the shadow (`shadow.ignore_*`)
  - a [background fill](#background-fill) between frame and window
    (`col.background`)
  - [window corner covers](#window-corner-covers) so the app looks cut like
    the frame (`corners`, `corners.size`, `corners.col`)
  - a [drop shadow](#drop-shadow) that follows the chamfers
  - [custom bar titles](#custom-bar-titles) (`title_rules`), same format as
    3LA-TitleBars, so both can share one rule list
- **`enabled` on every plugin.** `0` turns a plugin off live, without
  unloading it. Corners, TitleBars and MilSpec release their reserved space
  so windows snap back to their plain geometry; GlitchClose closes windows
  normally with no effect; Tint stops tinting.
- **Showcase images** in each plugin's section: [MilSpec](#3la-milspec),
  [Corners](#3la-corners), [TitleBars](#3la-titlebars), a before/after for
  [Tint](#3la-tint), and a real-time [GlitchClose](#3la-glitchclose) GIF.

## Demo

Three kitty terminals framed by 3LA-Corners, closed one by one through
`glitchclose:close`. Setup runs at 2×; the closes are real time:

![demo: three terminals closed with the shader glitch collapse](assets/glitchclose-demo.gif)

([full-quality mp4](assets/glitchclose-demo.mp4))

Every frame of the collapse is one fragment-shader pass: v-sync seam, slice
tearing, macroblock corruption, whole-frame ghost copies, chromatic aberration
and static, with the caption composited on top so it stays legible. The window's
own border stays clean throughout — the effect is inset strictly inside it.

A live desktop with 3LA-TitleBars on every window, switching between the
tiled layout and a fullscreen workspace. The kitty terminal keeps its own
title, while the `btop` and file-manager windows are relabeled by
`title_rules` to SYSTEM MONITOR and FILE SYSTEM EXPLORER:

![demo: 3LA-TitleBars on a live desktop, switching workspaces](assets/titlebars-demo.gif)

([full-quality mp4](assets/titlebars-demo.mp4))

## Requirements

### To run

| | |
|---|---|
| **Hyprland 0.56.2** | Matched on *commit hash*, not version string, so two 0.56.2 builds from different commits still mismatch — see [Rebuilding after a Hyprland update](#rebuilding-after-a-hyprland-update) |
| **A GL renderer** | 3LA-GlitchClose and 3LA-Tint need raw GL calls; on a Vulkan backend they raise a notification and disable themselves rather than misbehaving. 3LA-Corners and 3LA-TitleBars are unaffected. 3LA-MilSpec only goes through the renderer's own texture API (no raw GL), but has only been tested on the GL renderer. |

Nothing else. The built `.so` links only the C++ runtime — every Hyprland and GL
symbol (183 of them in 3LA-GlitchClose) is deliberately left undefined and
resolves against the running compositor at `dlopen`. That is why there is no
`-l` flag in the Makefiles, and why the plugin has no shared-library
dependencies of its own to install. The one exception is 3LA-MilSpec, which
links `libcairo` (`pkg-config --libs cairo`) to draw its frame; Hyprland itself
depends on cairo, so it is already installed and already mapped into the
compositor's process.

### To build

On Arch, everything comes from the official repos:

```sh
sudo pacman -S --needed hyprland gcc make binutils pkgconf
```

| package | provides | why |
|---|---|---|
| `hyprland` | `/usr/include/hyprland`, `/usr/share/pkgconfig/hyprland.pc`, `hyprpm` | the headers the plugins compile against |
| `gcc` | `g++` | the Makefiles ask for `-std=c++26` |
| `make` | `make` | build driver |
| `binutils` | `strip` | release builds are stripped |
| `pkgconf` | `pkg-config` | resolves the include paths |

Hyprland's own pkg-config `Requires` — aquamarine, hyprutils, hyprlang,
hyprgraphics, hyprcursor, cairo, pixman, libdrm, libglvnd, libxkbcommon,
libinput, wayland, libxcb, xcb-util-errors, freetype2, libpng — are all
dependencies of the `hyprland` package itself, so a working Hyprland install
already satisfies them. There is no separate `-devel` split on Arch: the
headers ship in the same package as the compositor.

**On other distributions** you need whatever package carries the Hyprland
headers and `hyprland.pc` (often `hyprland-devel`), or a Hyprland built from
source with its headers installed. Verify before building:

```sh
pkg-config --modversion hyprland          # must equal your running compositor
pkg-config --cflags hyprland pixman-1 libdrm
hyprctl version | head -3
```

If `pkg-config --modversion hyprland` fails, the headers are missing or not on
`PKG_CONFIG_PATH`, and every build below will fail with `hyprland.pc not found`.

### Optional

| what | Arch package | needed for |
|---|---|---|
| `python3` | `python` | regenerating a shader tuner's bundle (`3LA-GlitchClose-Viewer`) |
| `glslangValidator` | `glslang` | `make -C 3LA-GlitchClose-Viewer check`, which compiles the extracted GLSL headlessly |
| `update-desktop-database` | `desktop-file-utils` | `make -C 3LA-GlitchClose-Viewer install-desktop` |
| a WebGL2 browser | `google-chrome` / `chromium` / `firefox` | opening a tuner; the shaders are GLSL ES 3.00 so WebGL1 will not run them |
| `cmake`, `meson`, `ninja`, `git` | same names | only for the `hyprpm` route, which builds its own header copy |

## Building and installing

Two routes. **hyprpm** is less to remember and handles rebuilds; **manual** is
better if you are editing the plugins, since it skips the header rebuild.

### Option A — hyprpm

`hyprpm` reads [`hyprpm.toml`](hyprpm.toml) from the repo root, which declares
all five plugins and their `make` lines.

```sh
hyprpm update                                     # build headers for your Hyprland
hyprpm add https://github.com/ne0tt/Hypr-3LA
hyprpm enable 3LA-Corners
hyprpm enable 3LA-GlitchClose
hyprpm enable 3LA-TitleBars
hyprpm enable 3LA-Tint
hyprpm enable 3LA-MilSpec
hyprpm list                                       # confirm they show enabled: true
```

`hyprpm update` clones and configures Hyprland to produce its own header set,
so it needs the toolchain Hyprland builds with (`cmake`, `meson`, `ninja`,
`git`) and takes a few minutes the first time.

Then load them at every startup — in `hyprland.conf`:

```ini
exec-once = hyprpm reload -n
```

or in `hyprland.lua`, from inside your `hyprland.start` handler:

```lua
hl.exec_cmd("hyprpm reload -n")
```

The `commit_pins` entry in `hyprpm.toml` pairs a Hyprland commit with a
plugin-repo commit. The pin currently matches the `hyprland` package's
`GIT_COMMIT_HASH` (`efb5099…`). If you fork this repo, bump the *second* hash
after committing or `hyprpm update` will keep fetching the older revision.

### Option B — build from source

```sh
git clone https://github.com/ne0tt/Hypr-3LA
cd Hypr-3LA

make -C 3LA-Corners
make -C 3LA-GlitchClose
make -C 3LA-TitleBars
make -C 3LA-Tint
make -C 3LA-MilSpec
```

Each plugin directory is self-contained and has no build order between them.
Useful targets, identical across every plugin:

| target | effect |
|---|---|
| `make` / `make all` | optimised, stripped `.so` |
| `make debug` | adds `-g`; forces a clean rebuild first |
| `make clean` | removes the `.so` |

`-g` is left out of release builds on purpose: on 3LA-Corners it inflated the
`.so` roughly 33× (3.3 MB against ~100 KB) with no codegen benefit. The dynamic
exports Hyprland needs survive stripping either way, so `make debug` is only
worth it when you actually need a readable backtrace.

```sh
ls -la 3LA-Corners/3LA-Corners.so 3LA-GlitchClose/3LA-GlitchClose.so \
      3LA-TitleBars/3LA-TitleBars.so 3LA-Tint/3LA-Tint.so \
      3LA-MilSpec/3LA-MilSpec.so
```

Load them into the running compositor:

```sh
hyprctl plugin load "$PWD/3LA-Corners/3LA-Corners.so"
hyprctl plugin load "$PWD/3LA-GlitchClose/3LA-GlitchClose.so"
hyprctl plugin load "$PWD/3LA-TitleBars/3LA-TitleBars.so"
hyprctl plugin load "$PWD/3LA-Tint/3LA-Tint.so"
hyprctl plugin load "$PWD/3LA-MilSpec/3LA-MilSpec.so"
```

`hyprctl plugin load` needs an **absolute** path. To load at startup, from
`hyprland.lua`:

```lua
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-Corners/3LA-Corners.so")
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-GlitchClose/3LA-GlitchClose.so")
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-TitleBars/3LA-TitleBars.so")
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-Tint/3LA-Tint.so")
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-MilSpec/3LA-MilSpec.so")
```

Each plugin also ships an `autoload.sh` as a startup fallback: it loads the
`.so` via `hyprctl` only if it is not already present, then re-applies settings,
so it is safe to run repeatedly and covers the case where the `hl.plugin.load`
above doesn't take. See [Reference setup](#reference-setup-lua-config) for the
complete working wiring, including where to hook it.

Then apply settings and reload without restarting Hyprland:

```sh
hyprctl eval 'dofile(os.getenv("HOME") .. "/.config/hypr/config/plugins.lua")'
```

### Verifying the install

```sh
hyprpm list                                        # hyprpm route
hyprctl plugin list                                # either route — both should appear
hyprctl getoption plugin:3la_glitch_close:duration # a registered option proves init ran
```

A plugin that loaded but whose config values are missing (`no such option`)
almost always means an option was created but never passed to
`HyprlandAPI::addConfigValueV2`.

### Rebuilding after a Hyprland update

**All the plugins must be rebuilt every time Hyprland updates.** The API
commit hash is baked in at compile time from the headers and compared against
the running compositor's at load:

```cpp
if (std::string{__hyprland_api_get_hash()} != std::string{__hyprland_api_get_client_hash()})
    throw std::runtime_error("version mismatch");
```

A mismatch refuses to load with a notification instead of crashing the
compositor, so a stale build is an annoyance rather than a broken session.

```sh
# hyprpm route
hyprpm update && hyprpm reload

# manual route -- rebuild-plugins.sh cleans and rebuilds every plugin (skipping
# 3LA-GlitchClose-Viewer, which isn't a plugin), and --reload additionally
# unloads/reloads any of them that are currently running in the compositor
./rebuild-plugins.sh --reload

# ...or one plugin at a time by hand
make -C 3LA-GlitchClose clean && make -C 3LA-GlitchClose
hyprctl plugin unload "$PWD/3LA-GlitchClose/3LA-GlitchClose.so"
hyprctl plugin load   "$PWD/3LA-GlitchClose/3LA-GlitchClose.so"
```

Unload before load when reloading a rebuilt `.so` — loading over an already
loaded plugin of the same name is refused.

### The shader tuner (optional)

`3LA-GlitchClose-Viewer` is a WebGL2 tuner that extracts the GLSL, the option
list, the defaults and the ranges straight from the plugin sources, so it can
never drift from what the compositor actually draws.

```sh
make -C 3LA-GlitchClose-Viewer            # regenerate generated.js
make -C 3LA-GlitchClose-Viewer check      # compile the GLSL headlessly (needs glslang)
make -C 3LA-GlitchClose-Viewer open       # launch it in a browser
make -C 3LA-GlitchClose-Viewer install-desktop   # add to the app launcher
```

Re-run it after touching `shader.hpp` or `main.cpp`.

### Troubleshooting

| symptom | cause | fix |
|---|---|---|
| `hyprland.pc not found` | headers missing or off `PKG_CONFIG_PATH` | install the Hyprland headers package; check `pkg-config --modversion hyprland` |
| Notification: *version mismatch (rebuild against running Hyprland)* | plugin built against different headers than the running compositor | rebuild — see above |
| Notification: *requires the GL renderer* | Hyprland is on the Vulkan backend | 3LA-GlitchClose and 3LA-Tint cannot run there; 3LA-Corners and 3LA-TitleBars still work (3LA-MilSpec is untested there) |
| `hyprctl plugin load` returns an error | relative path, or already loaded | use an absolute path; `hyprctl plugin unload` first |
| `keyword can't work with non-legacy parsers` | `hyprctl keyword` against the Lua config | use `hyprctl eval 'hl.config{...}'` instead |
| `unknown config key 'plugin.…'` | plugin not loaded, or option never registered | check `hyprctl plugin list`; `pcall` does not hide this, so guard the block with `hl.get_config` (see [Configuration](#configuration)) |
| Builds fine, no visible effect | plugin loaded but settings never applied | re-run your `plugins.lua` via `hyprctl eval 'dofile(…)'` |

## Configuration

Everything in this README uses Hyprland's Lua config: settings are applied with
`hl.config`, wrapped in `pcall` at startup so applying config for a
not-yet-loaded plugin can't break the rest of the config:

```lua
pcall(hl.config, { plugin = {
    ["3la_corners"]      = { offset = 10, length = 40, thickness = 1 },
    ["3la_glitch_close"] = { duration = 700, strength = 1.0, text = "SIGNAL LOST" },
    ["3la_titlebars"]    = { height = 24, ["gap.top"] = 7, ["gap.bottom"] = 7 },
    ["3la_tint"]         = { match_class = "^(google-chrome)$", strength = 0.3 },
    ["3la_milspec"]      = { offset = 10, thickness = 2, chamfer = 15 },
} })
```

(The classic `hyprland.conf` ini keywords work too, with the same option names:
`plugin { 3la_glitch_close { duration = 700 } }`.)

Live-tweak any option without reloading (`hyprctl keyword` does not work with
the Lua parser — use `hl.config`):

```sh
hyprctl eval 'hl.config({ plugin = { ["3la_glitch_close"] = { strength = 2.0 } } })'
hyprctl getoption plugin:3la_glitch_close:duration   # inspect
hyprctl getoption plugin:3la_corners:enabled         # 0 or 1
```

`pcall` stops a Lua error, but Hyprland still shows its on-screen
"unknown config key" error bar for options of a plugin that isn't loaded. To
skip a block cleanly, guard it with `hl.get_config`, which returns `nil` for
an unregistered option (the reference setup below does this for MilSpec):

```lua
if hl.get_config("plugin:3la_milspec:enabled") ~= nil then
    hl.config({ plugin = { ["3la_milspec"] = { chamfer = 15 } } })
end
```

Prefer the plain `hyprctl getoption` output for regex options: `-j` prints
backslashes unescaped (e.g. `\.`), so the JSON fails to parse.

All the plugins read their colours from config on every frame, so feeding
matugen-generated globals into the `col.*` options re-themes a live effect with
no plugin reload.

## Reference setup (Lua config)

The exact wiring from a working config (`~/.config/hypr`):

**`hyprland.lua`** — register the plugins at startup, with an `autoload.sh`
fallback that loads via `hyprctl` and re-applies settings if the startup
registration doesn't take:

```lua
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-Corners/3LA-Corners.so")
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-GlitchClose/3LA-GlitchClose.so")
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-TitleBars/3LA-TitleBars.so")
hl.plugin.load(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-Tint/3LA-Tint.so")

hl.on("hyprland.start", function()
    hl.exec_cmd(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-Corners/autoload.sh")
    hl.exec_cmd(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-GlitchClose/autoload.sh")
    hl.exec_cmd(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-TitleBars/autoload.sh")
    hl.exec_cmd(os.getenv("HOME") .. "/git/Hypr-3LA/3LA-Tint/autoload.sh")
end)

require("config.colors")  -- matugen globals, before anything that uses them
require("config.plugins") -- plugin settings (see below)
```

**`config/plugins.lua`** — the full settings file from a working config
(colour globals such as `primary` come from `config/colors.lua`). Each block is
wrapped in `pcall`; MilSpec, which is not loaded at startup, is also guarded
with `hl.get_config`:

```lua
-- 3LA-Corners: corner brackets outside window borders
-- Repo: ~/git/Hypr-3LA/3LA-Corners (rebuild with `make` after Hyprland updates)
pcall(hl.config, {
    plugin = {
        ["3la_corners"] = {
            enabled = 0,                        -- 0 = off: no brackets, no reserved space
            offset = 6,                         -- gap (px) between window border and brackets
            length = 36,                        -- bracket arm length (px)
            thickness = 2,                      -- bracket line thickness (px)
            ["col.active"] = primary,           -- focused (0 = general:col.active_border)
            ["col.inactive"] = primary,         -- unfocused (0 = general:col.inactive_border); same as active = no focus change
            flash_duration = 150,               -- spawn flash on/off phase (ms, min 16)
            flash_on_focus = 1,                 -- also flash when a window gains focus (0 = off)
            focus_flash_count = 3,              -- flashes per focus change (0 = none)
            focus_flash_duration = 75,          -- focus flash on/off phase (ms, min 16)
            glow = 1,                           -- soft glow while a flash runs (0 = off)
            ["glow.size"] = 10,                 -- glow spread (px)
            ["glow.strength"] = 0.3,            -- glow intensity (0..1)
            ["col.glow"] = 0,                   -- 0 = follow the bracket color
            lines = 0,                          -- lines joining the brackets along each edge (0 = off)
            ["lines.thickness"] = 1,            -- joining line thickness (px), capped at bracket thickness
            ["lines.offset"] = 0,               -- shift lines inward (px, negative = outward)
            ["col.lines"] = on_secondary_fixed, -- joining line color (0 = follow the bracket color)
        }
    }
})

-- 3LA-GlitchClose: shader-driven glitch collapse when a window closes
-- Repo: ~/git/Hypr-3LA/3LA-GlitchClose (rebuild with `make` after Hyprland updates)
-- The effect plays over the still-open window; the real close goes out at
-- close_at * duration, then the overlay holds at full collapse until the window
-- is gone (capped by `hold`, default 1000 ms) before fading.
-- Super+Q is bound to hl.plugin.glitchclose.close() in config/keyboard/keybindings.lua.
pcall(hl.config, {
    plugin = {
        ["3la_glitch_close"] = {
            enabled = 1,                    -- 0 = off: Super+Q still closes, no effect
            duration = 400,                 -- burst length (ms, min 100)
            fade = 10,                      -- overlay fade-out after the window is gone (ms)
            close_at = 1.0,                 -- fraction of duration when the real close is sent
            strength = 0.38,                -- master displacement multiplier (0..5)
            aberration = 0.56,              -- RGB channel split (0..1)
            blocks = 0.45,                  -- slice tearing / macroblock corruption (0..1)
            noise = 0.00,                   -- digital static (0..1)
            scanlines = 0.8,                -- CRT scanline darkening (0..1)
            roll = 0.08,                    -- rolling bright bar (0..1)
            melt = 0.10,                    -- wavy tear boundary (0 = flat shear)
            tear = 0.55,                    -- v-sync frame tear (0..1)
            tear_speed = 2.0,               -- seam sweeps per burst (0..12); 1-2 = glitch, higher = strobe

            ghost = 0.8,                    -- sideways echo copies (0..1)
            vignette = 0.00,                -- edge darkening (0..1)
            backdrop_alpha = 0.5,           -- opacity the backdrop collapses to
            ["col.backdrop"] = secondary_container, -- 0 = black
            ["col.fringe1"] = col_accent,   -- glitch-band fringe 1 (0 = magenta)
            ["col.fringe2"] = primary,      -- glitch-band fringe 2 (0 = cyan)

            text = "SIGNAL LOST",           -- caption (empty = none)
            font = "monospace",             -- caption font family
            text_size = 20,                 -- caption size (pt)
            text_alpha = 0.85,              -- caption opacity (0 = hide text, keep plate)
            text_at = 0.15,                 -- fraction of duration when the caption appears
            text_blink = 10,                -- blink half-period (ms, 0 = steady, 300 = classic)
            text_padding = 14,              -- padding between caption and plate edge (px)
            text_bg_round = 0,              -- plate corner radius (px)
            text_bg_alpha = 0.55,           -- plate opacity (0 = no plate)
            ["col.text"] = 0,               -- caption color (0 = white)
            ["col.text_bg"] = col_accent,   -- plate color (0 = red)
        }
    }
})

-- Custom bar titles, shared by 3la_titlebars and 3la_milspec:
-- "class_regex,title_regex,text" rules joined by ";" (empty regex = any,
-- first match wins). SSH rules: kitty/cool-retro-term windows whose
-- title contains "@<host>" (from the local zsh preexec hook or the
-- remote PS1) show "Secure Shell: user@<host>". Add a host to
-- ssh_hosts to cover a new target.
local title_rules = table.concat((function()
    local rules = {
        ",^(btop)$,SYSTEM MONITOR",
        ",^(SPX-FileExplorer)$,FILE SYSTEM EXPLORER",
        ",^(qs-kitty-yay)$,SYSTEM UPDATE",
        ",^(GLKVM)$,REMOTE OPERATIONS SOCKET",
        ",^(com.microsoft.VSCode)$,VISUAL CODE"
    }
    local ssh_hosts = {
        "server1",
        "server2",
        "homelab"
    }
    local ssh_class = "^(kitty|cool-retro-term)$"
    for _, host in ipairs(ssh_hosts) do
        table.insert(rules, ssh_class .. ",@" .. host .. "\\b,   Secure Shell: user@" .. host)
    end
    return rules
end)(), ";")

-- 3LA-TitleBars: solid-color bar reserved above every window, with its title
-- Repo: ~/git/Hypr-3LA/3LA-TitleBars (rebuild with `make` after Hyprland updates)
-- SUPER+SHIFT+B toggles the bar on the focused window (config/keyboard/keybindings.lua).
pcall(hl.config, {
    plugin = {
        ["3la_titlebars"] = {
            enabled = 0,                    -- 0 = off: no bars, no reserved space
            height = 24,                    -- bar height (px)
            gap = 10,                       -- gap (px) above the bar; only used when gap.top = -1
            ["gap.top"] = 8,                -- gap (px) above the bar (-1 = use gap)
            ["gap.bottom"] = 3,             -- gap (px) between the bar and the window

            ["opacity.active"] = active_opacity,     -- bar/text/shadow opacity multiplier, focused
            ["opacity.inactive"] = inactive_opacity, -- same, unfocused

            ["col.active"] = col_accent,                   -- bar color, focused
            ["col.inactive"] = secondary_container_darker, -- bar color, unfocused (0 = follow col.active)

            shadow = 1,                     -- bar shadow, independent of the window's (0 = off)
            ["shadow.size"] = 9,            -- shadow spread (px)
            ["shadow.strength"] = 0.50,     -- shadow intensity (0..1)
            ["shadow.col"] = (shadow:gsub("ff%)$", "ee)")), -- matugen shadow with alpha ff -> ee (translucent black)

            ["text.size"] = 16,             -- title size (px)
            ["text.font"] = "Bebas Neue",   -- fontconfig family (fc-list); empty = misc:font_family
            ["text.col.active"] = primary,  -- title color, focused
            ["text.col.inactive"] = primary, -- title color, unfocused (0 = follow text.col.active)

            -- regex of window classes that never get a bar; a literal dot is \\. in Lua
            ignore_class = "^(" .. table.concat({
                "code",
                "chrome-www\\.youtube\\.com__-Default",
                "chrome-play\\.discoveryplus\\.com__-Default",
                "chrome-play\\.hbomax\\.com__-Default",
                "BambuStudio",
                "chrome-www.netflix.com__browse-Default",
                "chrome-www.bbc.co.uk__iplayer_live_bbcone-Default",
                "com.microsoft.VSCode",
                "brave-outlook\\.office\\.com__mail_0_-Default",
                "brave-teams\\.microsoft\\.com__v2_-Default",
                "chrome-claude.ai__new-Default",
            }, "|") .. ")$",
            -- named Chrome windows pinned to fixed workspaces (title = "^LEFT$" etc.
            -- in windowrules.lua); titles are unique, so no class needed
            ignore_title = "^(" .. table.concat({
                "LEFT",
                "RIGHT",
                "MAIN-LEFT",
                "MAIN-RIGHT",
                "BOTTOM-RIGHT",
            }, "|") .. ")$",
            title_rules = title_rules,      -- custom titles (shared local above)
        }
    }
})


-- 3LA-Tint: recolor grey/black/white pixels of matched windows toward col.tint
-- (hue + saturation used), remapping lightness into light_lo..light_hi.
-- Saturated pixels are left alone. Covers the window's subsurfaces and popups.
-- Repo: ~/git/Hypr-3LA/3LA-Tint (rebuild with `make` after Hyprland updates)
-- Colors come from config/colors.lua (matugen), so a theme change re-tints on
-- the next config reload.
-- strength is read from ~/.cache/matugen/tint-strength.lua, written by
-- ~/.config/hypr/scripts/set-tint-strength.sh (0.7 scheme-tonal-spot, else 0.3).
-- Callers: quickshell WallpaperDropdown.qml (after matugen) and
-- workspace40-HTB-windows.sh (apply_lime_theme / revert_theme). Fallback 0.3.
local ok, tint_strength = pcall(dofile, os.getenv("HOME") .. "/.cache/matugen/tint-strength.lua")
if not ok or type(tint_strength) ~= "number" then tint_strength = 0.3 end

pcall(hl.config, {
    plugin = {
        ["3la_tint"] = {
            enabled = 1,                    -- 0 = off
            -- Regexes of windows to tint; tinted if either matches. Read exact
            -- strings off `hyprctl clients`; a literal dot is \\. in Lua.
            match_title = "^(" .. table.concat({
                "Task Manager",
                --"Some Web App",
                "server1",
            }, "|") .. ")$",
            match_class = "^(" .. table.concat({
                --"cool-retro-term",
                "google-chrome",
                "chrome-claude.ai__new-Default",
            }, "|") .. ")$",
            ["col.tint"] = secondary_container, -- tint color (hue + saturation used)
            strength = tint_strength,       -- blend over greys (0..1); see header
            sat_lo = 0.1,                   -- chroma <= this: fully tinted
            sat_hi = 0.2,                   -- chroma >= this: untouched
            min_luma = 0.0,                 -- darker pixels stay untinted (0 = tint black too)
            light_lo = 0.06,                -- HSL lightness black maps to
            light_hi = 0.9,                 -- HSL lightness white maps to
        }
    }
})

-- 3LA-MilSpec: chamfered (top-left / bottom-right) HUD frame outside the window
-- border, with an optional title bar reserved inside it
-- Repo: ~/git/Hypr-3LA/3LA-MilSpec (rebuild with `make` after Hyprland updates)
-- Not loaded at startup, so guarded: hl.get_config returns nil for an option no
-- loaded plugin registered (see header).
if hl.get_config("plugin:3la_milspec:enabled") ~= nil then
pcall(hl.config, {
    plugin = {
        ["3la_milspec"] = {
            enabled = 1,                    -- 0 = off: no frame, no reserved space
            offset = 5,                     -- gap (px) between window border and frame
            -- regex: classes flush against the top stroke when they have no
            -- title bar (sides + bottom keep offset); empty = none
            ["offset.top_flush_class"] = "^(com\\.microsoft\\.VSCode)$",
            thickness = 1,                  -- frame stroke width (px)
            chamfer = 10,                   -- 45 deg cut (px) on top-left / bottom-right (0 = square)
            ["col.active"] = primary,       -- frame color, focused (0 = general:col.active_border)
            ["col.inactive"] = secondary_container_darker, -- frame color, unfocused (0 = col.active, else general:col.inactive_border)
            ["col.background"] = background, -- fill between frame and window (0 = transparent)
            -- Painted triangles over the window's own corners so the app looks cut
            -- like the frame (a cover, not real transparency): bottom-right always,
            -- top-left only on windows without a title bar.
            corners = 1,                    -- 0 = off
            ["corners.size"] = -1,          -- -1 = auto: even gap matching the sides; >= 0 = leg length (px)
            ["corners.col"] = 0,            -- cover color; 0 = follow col.background (opaque 0xff0a0f14 if that is 0 too)
            titlebar = 1,                   -- reserve a title bar inside the frame (0 = off)
            ["titlebar.height"] = 24,       -- bar height (px)
            ["titlebar.offset"] = 5,        -- gap (px) from the stroke's inner edge to the bar (top/left/right)
            ["titlebar.offset.top"] = -1,   -- per-side overrides (-1 = use titlebar.offset)
            ["titlebar.offset.left"] = -1,
            ["titlebar.offset.right"] = -1,
            ["titlebar.gap"] = 5,           -- gap (px) between the bar and the window
            ["col.titlebar.active"] = col_accent, -- bar fill, focused (0 = frame color)
            ["col.titlebar.inactive"] = secondary_container_darker, -- bar fill, unfocused (0 = col.titlebar.active, else frame color)
            ["titlebar.opacity.active"] = 1.0,   -- 0..1 multiplier on bar fill + title text, focused (0 = hidden, space kept)
            ["titlebar.opacity.inactive"] = 1.0, -- same, unfocused (stroke, background, corners, shadow unaffected)
            ["text.size"] = 16,             -- title size (px)
            ["text.font"] = "Bebas Neue",   -- empty = misc:font_family
            ["col.text.active"] = 0,        -- 0 = black or white, whichever contrasts with the bar
            ["col.text.inactive"] = 0,      -- 0 = col.text.active, else contrast
            ignore_class = "",              -- regex: classes never framed (empty = none)
            ignore_title = "",              -- regex: titles never framed
            title_rules = title_rules,      -- custom bar titles (same rules as 3la_titlebars, local above)
            ["titlebar.ignore_class"] = "", -- regex: classes framed but with no title bar
            ["titlebar.ignore_title"] = "", -- regex: titles framed but with no title bar
            frame = 1,                      -- draw the stroke (0 = hide; spacing and bar unchanged)
            ["frame.ignore_class"] = "",    -- regex: classes with no stroke (bar and spacing kept)
            ["frame.ignore_title"] = "",    -- regex: titles with no stroke (bar and spacing kept)
            -- Shadow follows the frame's outer edge (chamfers included), even with
            -- the stroke hidden. Same falloff as decoration:shadow; keep that
            -- disabled (decoration.lua) or both are drawn.
            shadow = 1,                     -- 0 = off
            ["shadow.range"] = 20,          -- shadow size (px, min 1)
            ["shadow.render_power"] = 4,    -- falloff power 1-4; higher = tighter
            ["shadow.sharp"] = 0,           -- 1 = hard edge, no falloff
            ["shadow.offset.x"] = 0,        -- px, positive = right
            ["shadow.offset.y"] = 0,        -- px, positive = down
            ["shadow.scale"] = 1.0,         -- 0..1, shrinks the shadow about its centre
            ["col.shadow.active"] = (shadow:gsub("ff%)$", "ee)")), -- same translucent black as decoration.lua
            ["col.shadow.inactive"] = 0,    -- 0 = follow col.shadow.active
            ["shadow.ignore_class"] = "",   -- regex: classes with no shadow
            ["shadow.ignore_title"] = "",   -- regex: titles with no shadow
        }
    }
})
end
```

Note that `hl.config` reports `unknown config key` **per key** for a plugin that
is not loaded, and does so *without raising* — the surrounding `pcall` does not
swallow it. Comment a plugin's settings block out at the same time as its
`hl.plugin.load` line, or every reload prints one error per option. For a
plugin that is only loaded sometimes, guard its block instead, as the
3LA-MilSpec block above does — `hl.get_config` returns `nil` for an option no
loaded plugin has registered.

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

# 3LA-MilSpec

Draws a stroked frame around every window, offset outside its core border,
with the **top-left and bottom-right corners chamfered** (cut at 45°) while
top-right and bottom-left stay square — a "mil-spec" HUD panel outline.
Optionally reserves a solid **title bar inside the frame**, between the
frame's top edge and the window, showing the window's title in uppercase.

![3LA-MilSpec with title bar](assets/milspec.png)

*With title bar.*

![3LA-MilSpec without title bar](assets/milspec-no-titlebar.png)

*Without title bar: both corners cut via the corner covers.*

The frame (plus the optional background fill, the window corner covers, and
the bar's fill so it follows the top-left cut) is drawn with
cairo into one antialiased texture at the monitor's pixel scale, cached per
window, and only redrawn when its size, scale, colours or the relevant options
change.

## Config

Defaults shown:

```lua
hl.config({ plugin = { ["3la_milspec"] = {
    enabled = 1,          -- 0 = off: no frame and no reserved space, applied
                          -- live (windows snap back to their plain geometry)
    offset = 10,          -- gap (px) between the window's border and the frame
    ["offset.top_flush_class"] = "",
                          -- regex of window classes that sit flush against
                          -- the top stroke (no `offset` on the top edge only)
                          -- while they have no title bar; empty = none.
                          -- See Layout.
    thickness = 2,        -- frame stroke width (px)
    chamfer = 15,         -- leg length (px) of the 45° cut on the top-left and
                          -- bottom-right corners, measured on the frame's
                          -- outer edge (0 = square corners)

    ["col.active"] = 0,   -- focused-window frame color.
                          -- 0 = follow general:col.active_border (first color);
                          -- otherwise e.g. "rgba(33ccffee)"
    ["col.inactive"] = 0, -- unfocused-window frame color.
                          -- 0 = follow col.active if that is set, else
                          -- general:col.inactive_border (first color)
    ["col.background"] = 0, -- fill between the frame and the window (+ border),
                            -- under the title bar and stroke. 0 = transparent;
                            -- the color's own alpha applies, e.g. "rgba(0a141c99)"

    corners = 1,               -- paint over the window's own corners so the app
                               -- looks cut like the frame: bottom-right always,
                               -- top-left only when the window has no title bar
                               -- (0 = off). A painted cover, not real transparency.
    ["corners.size"] = -1,     -- -1 = auto: even gap matching the sides (the
                               -- window's cut runs parallel to the frame's
                               -- diagonal at the same gap as the straight
                               -- edges); >= 0 = leg length (px), 0 = no cover
    ["corners.col"] = 0,       -- cover color. 0 = follow col.background; if that is
                               -- 0 too, an opaque near-black 0xff0a0f14. Alpha respected

    titlebar = 0,              -- reserve a title bar inside the frame (0 = off, 1 = on)
    ["titlebar.height"] = 20,  -- bar height (px)
    ["titlebar.offset"] = 4,   -- gap (px) between the frame stroke's inner
                               -- edge and the bar, on its top/left/right
    ["titlebar.offset.top"] = -1,   -- per-side overrides of titlebar.offset
    ["titlebar.offset.left"] = -1,  -- (-1 = use titlebar.offset)
    ["titlebar.offset.right"] = -1,
    ["titlebar.gap"] = 4,      -- gap (px) between the bottom of the bar and
                               -- the window's border
    ["col.titlebar.active"] = 0,   -- focused-window bar fill color.
                                   -- 0 = follow the frame's col.active color
    ["col.titlebar.inactive"] = 0, -- unfocused-window bar fill color.
                                   -- 0 = follow col.titlebar.active if that is
                                   -- set, else the frame's col.inactive color
    ["titlebar.opacity.active"] = 1.0,   -- opacity multiplier (0..1) on the bar
                                         -- fill and title text, focused window
    ["titlebar.opacity.inactive"] = 1.0, -- same, unfocused windows. 0 = no bar
                                         -- or text, but the space stays reserved.
                                         -- Stroke, col.background, corner covers
                                         -- and shadow are not affected
    ["text.size"] = 12,        -- title font size (px)
    ["text.font"] = "",        -- title font family (empty = follow misc:font_family)
    ["col.text.active"] = 0,   -- focused-window title text color.
                               -- 0 = black or white, whichever contrasts
                               -- with the bar
    ["col.text.inactive"] = 0, -- unfocused-window title text color.
                               -- 0 = follow col.text.active if that is set,
                               -- else black/white contrast

    ignore_class = "",    -- regex of window classes to never frame (empty = none)
    ignore_title = "",    -- regex of window titles to never frame (empty = none)

    ["titlebar.ignore_class"] = "", -- regex of window classes that keep the
                                    -- frame but get no title bar (empty = none)
    ["titlebar.ignore_title"] = "", -- regex of window titles that keep the
                                    -- frame but get no title bar (empty = none)

    title_rules = "",               -- custom bar titles, same format as
                                    -- 3la_titlebars:title_rules (empty = none);
                                    -- see "Custom bar titles" below

    frame = 1,                      -- draw the frame stroke (0 = hide it on every
                                    -- window; spacing and title bar unchanged)
    ["frame.ignore_class"] = "",    -- regex of window classes that get no frame
                                    -- stroke (empty = none)
    ["frame.ignore_title"] = "",    -- regex of window titles that get no frame
                                    -- stroke (empty = none)

    shadow = 1,                     -- drop shadow following the frame (0 = off)
    ["shadow.range"] = 30,          -- shadow size (px), as decoration:shadow:range
    ["shadow.render_power"] = 4,    -- falloff power 1-4 (higher = tighter)
    ["shadow.sharp"] = 0,           -- 1 = hard-edged, no falloff
    ["shadow.offset.x"] = 0,        -- shift (px); negative = left
    ["shadow.offset.y"] = 0,        -- shift (px); negative = up
    ["shadow.scale"] = 1.0,         -- 0.0-1.0, as decoration:shadow:scale
    ["col.shadow.active"] = "rgba(000000ee)", -- focused-window shadow color
    ["col.shadow.inactive"] = 0,    -- unfocused; 0 = follow col.shadow.active
    ["shadow.ignore_class"] = "",   -- regex of window classes that get no shadow
    ["shadow.ignore_title"] = "",   -- regex of window titles that get no shadow
} } })
```

### Custom bar titles

`title_rules` replaces the text on the title bar without touching the window's
real title (used elsewhere for matching, alt-tab, etc.). It uses the same
format and matching as 3LA-TitleBars' [`title_rules`](#custom-titles), so one
string can drive both plugins:

```lua
-- 'class_regex,title_regex,override text' rules, separated by ';'.
-- Either regex may be empty to match any class/title. First match wins.
title_rules = ",^(btop)$,SYSTEM MONITOR;^(firefox)$,,WEB BROWSER",
```

A window whose title matches `^(btop)$` (any class) shows "SYSTEM MONITOR"; a
`firefox` window (any title) shows "WEB BROWSER"; every other window keeps its
own title.

The rules:

- **Three fields, always.** `class_regex,title_regex,override text`. Only the
  first two commas split fields, so the override text may contain commas. A
  2-field shorthand like `^(btop)$,SYSTEM MONITOR` is read as a *class* regex
  with no override field and skipped — leave the class empty instead:
  `,^(btop)$,SYSTEM MONITOR`.
- **Empty regex = any.** An empty class or title field matches every window.
  Both regexes must match for the rule to apply.
- **Search, not full match.** Regexes are `std::regex_search` (ECMAScript), so
  `kitty` also matches `kitty-dropdown`; anchor with `^...$` for exact names.
- **First match wins.** Rules are tried left to right; put specific rules
  before broad ones.
- **One leading space is trimmed** from the title regex and the override text,
  so `^(kitty)$, ^(btop)$, SYSTEM MONITOR` reads the same as the unspaced form.
  Further spaces are kept (the reference setup uses this to indent SSH titles).
- **Empty override text** (`^(foo)$,,`) leaves the bar drawn but blank.
- **Bad rules are skipped individually.** A rule missing a field or holding an
  invalid regex raises an in-session notification naming it and why; the rest
  of the list still applies.
- **Matching uses the real class/title**, so `ignore_*`, `titlebar.ignore_*`,
  `frame.ignore_*` and `shadow.ignore_*` all see the real title, never the
  override.
- **Live.** The string is re-parsed only when it changes, and a window's bar
  text is re-resolved whenever its title or class changes (e.g. a terminal
  running `btop` or `ssh`).
- **Bar only.** Overrides only show where a bar is drawn: nothing with
  `titlebar = 0`, on windows matched by `titlebar.ignore_*`, or on a window
  whose bar was [toggled off](#toggling-the-title-bar-per-window).
- **Styled like any title:** uppercased (ASCII only) and drawn with `text.*` /
  `col.text.*`, faded by `titlebar.opacity.*`.

To keep both plugins in step, build the string once in a Lua local and pass it
to both, as the [reference setup](#reference-setup-lua-config) does, including
a loop that generates one `Secure Shell: user@<host>` rule per SSH host.

### Drop shadow

Hyprland's own `decoration:shadow` is a rounded rectangle around the window
itself, so it sits inside the frame and ignores the chamfers. The plugin's
shadow uses the same falloff as Hyprland's `shadow.glsl`
(`alpha = ((range - d) / range) ^ render_power` for distance `d` outside the
shape) and the same geometry order (grow by `range`, scale about the centre,
then offset). It is measured from the frame's **outer edge**, so it follows
both 45° cuts and the title bar band.

- Keep `decoration:shadow:enabled = false`, or both shadows are drawn.
- Drawn on the BOTTOM decoration layer and cut out where the frame is: it only
  shows **outside** the frame, never in the gap between frame and window.
- `frame = 0` / `frame.ignore_*` hide only the stroke; the shadow still traces
  the frame shape. `enabled = 0` or `ignore_class` / `ignore_title` (no frame
  at all) also remove the shadow.
- `shadow.scale` shrinks the range-grown box like Hyprland does, so values
  well below 1 pull the shadow inside the frame and hide it.
- Like the frame, it ignores the renderer's fade alpha, so it does not fade
  out from under a frame that stays opaque.
- Built on the CPU into up to 4 cached textures covering only the ring around
  the frame; rebuilt when size, scale, colour or a shadow option changes.
- Every option applies live via `hyprctl eval`.

### Background fill

`col.background` paints the space between the frame and the window, like the
panel behind a HUD frame. Default `0` = transparent (nothing drawn).

- Fills the frame's chamfered outline up to the stroke's **inner** edge, so a
  translucent stroke isn't blended over it. With the stroke hidden
  (`frame = 0` / `frame.ignore_*`) it reaches out to where the stroke's outer
  edge would be, keeping the same chamfered silhouette.
- The window and Hyprland's border are cut out, with the border's outer
  corner shape (`rounding` + `border_size`, honouring `rounding_power`; square
  when the window isn't rounded). Nothing is painted behind the window, so a
  translucent window never shows the fill through it, and the corners outside
  a rounded window are filled (no transparent notches).
- Drawn into the same cached texture as the frame, under the title bar and
  the stroke. The colour's alpha is respected (`"rgba(ff000080)"` = 50 %).
- Covers anything else sitting in the gap that draws underneath it; ring
  decorations drawn on top (e.g. 3LA-Corners' lines) still show.
- Windows with no frame (`enabled = 0`, `ignore_class` / `ignore_title`) get
  no fill. Applies live via `hyprctl eval`.

```sh
hyprctl eval 'hl.config({ plugin = { ["3la_milspec"] = { ["col.background"] = "rgba(0a141c99)" } } })'
hyprctl eval 'hl.config({ plugin = { ["3la_milspec"] = { ["col.background"] = 0 } } })'   # back to transparent
```

### Window corner covers

The frame is chamfered but the app inside it is still a square window, so a
square window corner would sit inside a cut frame corner. `corners = 1` (the
default) paints a 45° triangle **over** the window's own corner so the app
looks cut too:

- **Bottom-right:** always covered.
- **Top-left:** covered only when the window shows **no** MilSpec title bar
  (`titlebar = 0`, matched by `titlebar.ignore_class` / `titlebar.ignore_title`,
  or toggled off with `3la_milspec:toggle`). With the bar shown, the bar
  carries the top-left cut and the window's corner sits below it.

It is a **painted cover, not a transparency cut**: whatever is behind the
window does not show through. It reads as a cut when its colour matches what
surrounds it, which is why it follows `col.background` by default.

- Triangle legs run along the window's edges, measured on the window **plus**
  Hyprland's border (so the border's corner is covered too), with only the
  diagonal antialiased. Covers are clamped to half the window's shorter side.
- `corners.size = -1` (default) is **auto: even gap matching the sides**. Per
  corner, from the real geometry (stroke `thickness` T, `chamfer` C, and the
  gaps gA / gB from the stroke's inner edge to the window + border on the two
  edges meeting there, title-bar band and `topExtra` included), the window's
  cut is placed parallel to the frame's inner diagonal at the perpendicular
  gap d = min(gA, gB). The leg is
  `s = C + (T + d)·√2 − (T + gA) − (T + gB)`, i.e. `C − (2 − √2)·(T + g)`
  when both gaps equal g (thickness 1, offset 5, chamfer 10 → 6.5 px). If
  s ≤ 0 the window's corner already clears the diagonal and no cover is
  drawn. The same geometry is used with the stroke hidden (`frame = 0`).
  A value ≥ 0 is a manual leg length in px (0 = no cover).
- When `chamfer` is large compared with `offset` + `thickness`, the frame's
  diagonal crosses the window's corner, so part of the cover sits outside the
  frame's cut (in the cover colour) — the window's corner pokes out there
  anyway without covers.
- Colour: `corners.col`, else `col.background`, else an opaque near-black
  `0xff0a0f14` (so the cover is never invisible when the background fill is
  off). Always drawn fully opaque: the colour's alpha is ignored so the
  window's corner never shows through.
- No line is drawn along the diagonal; the frame stroke stays on top of the
  cover where the window's corner pokes past the frame's diagonal.
- Rounded windows: the triangle covers the rounded corner area as long as the
  leg is longer than the rounding; with a larger `rounding` a sliver of the
  arc can show past the diagonal.
- Skipped on fullscreen windows and windows with no frame (`enabled = 0`,
  `ignore_class` / `ignore_title`). `frame = 0` / `frame.ignore_*` (stroke
  only) keep the covers.
- Drawn into the same cached frame texture (OVER decoration layer: above the
  window surface, below its popups), so it costs nothing extra per frame; the
  texture is rebuilt only when the cover's geometry or colour changes. Every
  option applies live via `hyprctl eval`.

```sh
hyprctl eval 'hl.config({ plugin = { ["3la_milspec"] = { ["corners.size"] = 20, ["corners.col"] = "rgba(0a141cff)" } } })'
hyprctl eval 'hl.config({ plugin = { ["3la_milspec"] = { corners = 0 } } })'   # square window corners again
```

Live-tweak and inspect (the Lua parser rejects `hyprctl keyword`):

```sh
hyprctl eval 'hl.config({ plugin = { ["3la_milspec"] = { chamfer = 30, titlebar = 1, ["titlebar.height"] = 22 } } })'
hyprctl getoption plugin:3la_milspec:chamfer
```

From a working `config/plugins.lua`, themed from matugen globals:

```lua
-- 3LA-MilSpec: chamfered (top-left / bottom-right) HUD frame outside the window
-- border, with an optional title bar reserved inside it
-- Repo: ~/git/Hypr-3LA/3LA-MilSpec (rebuild with `make` after Hyprland updates)
-- Not loaded at startup, so guarded: hl.get_config returns nil for an option no
-- loaded plugin registered (see header).
if hl.get_config("plugin:3la_milspec:enabled") ~= nil then
pcall(hl.config, {
    plugin = {
        ["3la_milspec"] = {
            enabled = 1,                    -- 0 = off: no frame, no reserved space
            offset = 5,                     -- gap (px) between window border and frame
            -- regex: classes flush against the top stroke when they have no
            -- title bar (sides + bottom keep offset); empty = none
            ["offset.top_flush_class"] = "^(com\\.microsoft\\.VSCode)$",
            thickness = 1,                  -- frame stroke width (px)
            chamfer = 10,                   -- 45 deg cut (px) on top-left / bottom-right (0 = square)
            ["col.active"] = primary,       -- frame color, focused (0 = general:col.active_border)
            ["col.inactive"] = secondary_container_darker, -- frame color, unfocused (0 = col.active, else general:col.inactive_border)
            ["col.background"] = background, -- fill between frame and window (0 = transparent)
            -- Painted triangles over the window's own corners so the app looks cut
            -- like the frame (a cover, not real transparency): bottom-right always,
            -- top-left only on windows without a title bar.
            corners = 1,                    -- 0 = off
            ["corners.size"] = -1,          -- -1 = auto: even gap matching the sides; >= 0 = leg length (px)
            ["corners.col"] = 0,            -- cover color; 0 = follow col.background (opaque 0xff0a0f14 if that is 0 too)
            titlebar = 1,                   -- reserve a title bar inside the frame (0 = off)
            ["titlebar.height"] = 24,       -- bar height (px)
            ["titlebar.offset"] = 5,        -- gap (px) from the stroke's inner edge to the bar (top/left/right)
            ["titlebar.offset.top"] = -1,   -- per-side overrides (-1 = use titlebar.offset)
            ["titlebar.offset.left"] = -1,
            ["titlebar.offset.right"] = -1,
            ["titlebar.gap"] = 5,           -- gap (px) between the bar and the window
            ["col.titlebar.active"] = col_accent, -- bar fill, focused (0 = frame color)
            ["col.titlebar.inactive"] = secondary_container_darker, -- bar fill, unfocused (0 = col.titlebar.active, else frame color)
            ["titlebar.opacity.active"] = 1.0,   -- 0..1 multiplier on bar fill + title text, focused (0 = hidden, space kept)
            ["titlebar.opacity.inactive"] = 1.0, -- same, unfocused (stroke, background, corners, shadow unaffected)
            ["text.size"] = 16,             -- title size (px)
            ["text.font"] = "Bebas Neue",   -- empty = misc:font_family
            ["col.text.active"] = 0,        -- 0 = black or white, whichever contrasts with the bar
            ["col.text.inactive"] = 0,      -- 0 = col.text.active, else contrast
            ignore_class = "",              -- regex: classes never framed (empty = none)
            ignore_title = "",              -- regex: titles never framed
            title_rules = title_rules,      -- custom bar titles, shared with 3la_titlebars (see Reference setup)
            ["titlebar.ignore_class"] = "", -- regex: classes framed but with no title bar
            ["titlebar.ignore_title"] = "", -- regex: titles framed but with no title bar
            frame = 1,                      -- draw the stroke (0 = hide; spacing and bar unchanged)
            ["frame.ignore_class"] = "",    -- regex: classes with no stroke (bar and spacing kept)
            ["frame.ignore_title"] = "",    -- regex: titles with no stroke (bar and spacing kept)
            -- Shadow follows the frame's outer edge (chamfers included), even with
            -- the stroke hidden. Same falloff as decoration:shadow; keep that
            -- disabled (decoration.lua) or both are drawn.
            shadow = 1,                     -- 0 = off
            ["shadow.range"] = 20,          -- shadow size (px, min 1)
            ["shadow.render_power"] = 4,    -- falloff power 1-4; higher = tighter
            ["shadow.sharp"] = 0,           -- 1 = hard edge, no falloff
            ["shadow.offset.x"] = 0,        -- px, positive = right
            ["shadow.offset.y"] = 0,        -- px, positive = down
            ["shadow.scale"] = 1.0,         -- 0..1, shrinks the shadow about its centre
            ["col.shadow.active"] = (shadow:gsub("ff%)$", "ee)")), -- same translucent black as decoration.lua
            ["col.shadow.inactive"] = 0,    -- 0 = follow col.shadow.active
            ["shadow.ignore_class"] = "",   -- regex: classes with no shadow
            ["shadow.ignore_title"] = "",   -- regex: titles with no shadow
        }
    }
})
end
```

## Layout

Space is *reserved* on every edge, like the core border: tiled windows shrink
and floating windows are inset so nothing overlaps the frame. From the window
outward:

```
left / right / bottom:  window | border | offset | thickness
top (no bar):           window | border | offset | thickness
top (no bar, flush):    window | border | thickness
top (bar):              window | border | titlebar.gap | titlebar.height | titlebar.offset.top | thickness
```

"No bar" means `titlebar = 0`, a `titlebar.ignore_class` /
`titlebar.ignore_title` match, or the bar toggled off with
`3la_milspec:toggle`. "Flush" is a no-bar window whose class matches
`offset.top_flush_class`: it drops `offset` on the top edge only and sits
directly against the top stroke, while the sides and bottom keep `offset`.
It is meant for apps that draw their own title or menu bar (VS Code, for
example), so the frame hugs that bar instead of leaving a gap above it. A
window with a MilSpec bar is unaffected: the bar band is reserved as usual.

With the top gap gone, the window's top-left corner sits further into the
frame's chamfer. With the default auto `corners.size = -1`, the top-left
corner cover is measured from the real gaps, so it grows to match. Like
every option, the regex applies live, and a class change re-evaluates it:

```sh
hyprctl eval 'hl.config({ plugin = { ["3la_milspec"] = { ["offset.top_flush_class"] = "^(com\\.microsoft\\.VSCode|kitty)$" } } })'
hyprctl eval 'hl.config({ plugin = { ["3la_milspec"] = { ["offset.top_flush_class"] = "" } } })'   # off
```

With the bar on, it takes the place of `offset` on the top edge: the bar sits
`titlebar.offset.top` below the top stroke's inner edge and
`titlebar.gap` above the window. Across the width it is inset
`titlebar.offset.left` / `.right` from the side strokes' inner edges (the side
strokes themselves stay `offset` out from the window), so it floats inside the
outline like a launcher-panel header.

The bar gets its own 45° cut at the top-left, parallel to the frame's and the
same perpendicular distance in from the stroke as the tighter of
`titlebar.offset.top` / `.left`, so the margin along the diagonal matches the
margin along the edges. With an inset large enough to clear the frame's cut
entirely (or `chamfer = 0`), the bar's corner is simply square. The title is
left-aligned, padded clear of the bar's cut, ellipsised to fit, and vertically
centred in the bar.

## Toggling the title bar per-window

`3la_milspec:toggle` flips the title bar on the currently **focused** window
only (independent of `titlebar.ignore_class`/`titlebar.ignore_title`, and
reversible — press again to bring it back). The frame stays; only the bar and
its reserved band go. It does nothing while `titlebar = 0`. Exposed the same
two ways as 3LA-TitleBars' toggle, so one keybind can drive both:

```lua
-- classic dispatch string (hyprland.conf-style bind)
bind = SUPER SHIFT, B, 3la_milspec:toggle

-- Lua keybind (each hl.plugin.<name> only exists while that plugin is loaded, hence one pcall each)
hl.bind("SUPER + SHIFT + B", function()
    pcall(function() hl.plugin.titlebars.toggle() end)
    pcall(function() hl.plugin.milspec.toggle() end)
end, { description = "Toggle title bar on active window" })
```

As with TitleBars, use `hyprctl eval 'hl.plugin.milspec.toggle()'` for one-off
testing on a Lua-parsed config.

## Notes

- Every option is registered with a layout-refresh flag, so a live
  `hyprctl eval 'hl.config(...)'` change re-reserves the space immediately.
  `hl.config` never emits `config.reloaded` (only a file reload does), so
  without that flag a change to `offset`/`thickness`/`titlebar` would keep
  the old reserved space until the next file reload, and the bar would paint
  over the window's own top pixels.
- **Stacking.** The frame is a sticky decoration at priority 9980: outside
  the core border (10000) and 3LA-Corners (9990). Its position comes from
  the decoration positioner, so whatever sits inside it (border size, a
  `noborder` rule, 3LA-Corners' ring) is accounted for automatically.
- **With 3LA-TitleBars loaded**, that bar is stacked outside this frame, and
  the frame grows upward to enclose it (the same "extra top reserved" rule
  3LA-Corners uses). Running both title bars at once gives two bars; pick one.
- **With 3LA-Corners loaded**, the brackets sit on their own ring inside the
  frame, but Corners pushes its *top* brackets up past every top reservation
  outside it (meant for 3LA-TitleBars), so they collide with this frame's top
  edge. Running both is not recommended.
- Like 3LA-Corners, the frame ignores the renderer's opacity multiplier
  (window opacity rules, `decoration:active_opacity` / `inactive_opacity`):
  only the configured colors' own alpha matters. The title bar fill and its
  text are additionally scaled by the plugin's own `titlebar.opacity.active` /
  `titlebar.opacity.inactive` (blended over `col.background`, or whatever is
  behind if that is transparent); at 0 neither is drawn but the bar's space
  stays reserved.
- Not drawn on fullscreen windows. Closing a window clears its frame straight
  away: `window.close` damages it while the geometry is still valid, and a
  client that exits on its own falls back to damaging the whole output.
- `ignore_class` / `ignore_title` drop the frame **and** its reserved space,
  and are re-evaluated on `window.title` / `window.class_` events.
- `titlebar.ignore_class` / `titlebar.ignore_title` only drop the title bar:
  a matching window keeps its frame, and its top edge falls back to the plain
  `offset` + `thickness` (the bar's `titlebar.offset.top` + `titlebar.height`
  + `titlebar.gap` is released). The frame-level filters above win when both
  match. Re-evaluated live on title/class changes, so a window whose title
  starts or stops matching gains or loses its bar and repositions on the spot.
- `frame = 0` / `frame.ignore_class` / `frame.ignore_title` hide only the
  frame **stroke**. Nothing else changes: the reserved space, the bar's
  position, inset and its own 45° cut all stay exactly where they were, so
  toggling the stroke never moves a window — you get a floating header bar
  with no outline. The frame-level `ignore_class` / `ignore_title` still win
  (no decoration at all), and `titlebar.ignore_*` still removes the bar.
  A window whose stroke is hidden **and** whose bar is ignored therefore
  draws nothing at all but keeps its reserved `offset` + `thickness` margin
  on every edge.
- All six filters are C++ `std::regex` searches against the window's class /
  title, recompiled only when the pattern changes. An invalid pattern disables
  that filter (nothing is ignored) instead of erroring; no notification is
  raised.
- Cost: one RGBA texture per framed window, the size of the frame in physical
  pixels (≈ 33 MB of VRAM for a 4K-sized window). It is rebuilt only when
  something changes, but that includes every frame of an animated resize and
  focus change that switches its colour. A window straddling two monitors with
  different scales rebuilds it on every frame for as long as it straddles them.

---

# 3LA-Corners

Draws decorative corner brackets outside every window's border. Each corner gets
two line segments (horizontal + vertical) offset outside the window border, like
a targeting reticle. The focused window and unfocused windows can be given
different colors, and either window spawn or focus gain can trigger a flash.

![3LA-Corners](assets/corners.png)

*Corner brackets with glow around a focused kitty (1 px Hyprland border on).*

## Config

Defaults shown:

```lua
hl.config({ plugin = { ["3la_corners"] = {
    enabled = 1,          -- 0 = off: no brackets and no reserved space, applied
                          -- live (windows snap back to their plain geometry)
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

    lines = 0,               -- lines joining the brackets along each edge (0 = off)
    ["lines.thickness"] = 1, -- joining line thickness (px), capped at `thickness`
    ["lines.offset"] = 0,    -- shift lines inward toward the window (px, negative = outward)
    ["col.lines"] = 0,       -- 0 = follow the bracket's own color
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
disables the spawn flash only.

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

### Joining lines

`lines = 1` draws a line between the arm tips of neighbouring brackets on every
edge, turning the brackets into a full frame. The lines sit flush with the
brackets' outer edge, are `lines.thickness` px thick (never thicker than the
brackets), and flash along with them. `lines.offset` shifts them inward toward
the window by that many px; a negative value pushes them outward. `col.lines = 0` follows the bracket's own
colour; set it to give the lines their own colour.

### Active / inactive styling

The brackets render at **full alpha regardless of focus**, fully decoupled from
window opacity. The `a` alpha the renderer passes to the decoration is ignored
outright, so none of these affect the brackets:

- `decoration:active_opacity` / `decoration:inactive_opacity`
- per-window `opacity` window rules
- window fade-in/out and workspace-move alpha

Only two things set the final alpha: the alpha channel of the configured color
(`col.active` / `col.inactive`) and the flash envelope. The trade-off is that
brackets pop in at full opacity when a window opens rather than fading with it.

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
- The top pair of brackets automatically frames the outside of any other
  plugin's reserved top-edge space (e.g. 3LA-TitleBars' bar), rather than
  sitting flush against the window's own — now lower — top edge. This reads
  the decoration positioner's total top-reserved extent and subtracts this
  plugin's own margin and the core border (itself a reserved decoration), so
  it stays put with 0 extra offset when nothing else reserves top space. The
  core Hyprland window border is unaffected either way — only the brackets
  move.
- With 3LA-TitleBars, the gap between the top brackets and the bar's top edge
  is `gap.top - thickness`, and the side gap between the brackets and the
  window is `offset`. Set them equal (e.g. `offset = 8`, `thickness = 2`,
  `gap.top = 10`) for an even frame all round.
- Brackets are hidden on fullscreen windows. A burst armed on such a window still
  expires on its own schedule, since expiry is time-based rather than frame-driven.
- Active state is read per-frame from `Desktop::focusState()->isWindowActive()`,
  and the plugin listens on the `window.active` event to damage both the window
  losing focus and the one gaining it, so brackets repaint immediately on focus
  change. `flash_on_focus` reuses that same listener, subject to the spawn-burst
  priority rule above.
- While a flash is running the decoration damages itself every frame to drive the
  animation, so `flash_on_focus = 1` costs a short burst of redraws per focus
  change. On a heavily loaded GPU prefer a low `focus_flash_count`.
- Closing a window no longer leaves its brackets stuck on screen. `window.close`
  damages them while the window's geometry is still valid — that covers a
  compositor-initiated close (a dispatcher, `killactive`). A client that exits on
  its own (typed `exit`, its own quit shortcut, a plain kill) never fires
  `window.close`; only `window.destroy` reaches it, by which point the geometry
  needed to compute a damage box is already gone, so the decoration instead
  replays the whole output it was last drawn on — a precise cached sub-box was
  observed to not actually repaint on a non-focused monitor.

---

# 3LA-GlitchClose

Plays a CCTV "signal lost" collapse over a window when it closes: the window's
own content tears apart, static and a backdrop ramp in over it, a `SIGNAL LOST`
caption appears, and only then does the window actually close.

![3LA-GlitchClose](assets/glitchclose-showcase.gif)

*A lone kitty closed with `hl.plugin.glitchclose.close()`, real time
(`duration 400`): window, glitch collapse, then bare wallpaper.
([mp4](assets/glitchclose-showcase.mp4); the multi-window demo is under
[Demo](#demo).)*

The whole visual is one GLSL fragment shader — the window snapshot goes through
a single pass into the effect's own framebuffer, which is then composited. So:

- displacement is **continuous and per-pixel**
- the v-sync frame tear is real: the seam has the content on each side evaluated
  at a different animation step
- chromatic aberration is a genuine per-channel UV offset
- static is sub-pixel and never repeats
- one draw call per frame
- every knob is a live uniform, so re-running matugen re-themes a running effect
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
rotate the finished effect a second time. The mapping is built once per effect
and rebuilt only if the monitor is rescaled or rotated under a live collapse.

Program binding goes through `g_pHyprOpenGL->useShader()` rather than raw
`glUseProgram()`: `CHyprOpenGLImpl` caches the bound program to skip redundant
binds, and desyncing that cache breaks the *next* Hyprland draw. Viewport and
blend go through the renderer wrappers for the same reason.

## The `glitchclose:close` dispatcher (use this to close windows)

Closing first and animating afterwards means the layout has already re-tiled and
the effect plays over the neighbours that took the space. The dispatcher fixes
the ordering: the collapse plays over the still-open window, and the real close
request goes out at `close_at` × `duration` — by default on the frame the burst
ends.

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

The tile slot is held for the whole burst, so nothing is drawn over the
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
An application that simply **exits on its own** never emits `window.close` at
all, so it gets no effect — this is inherent to the event.

**Workaround for shell exits:** a terminal running `exit` closes its own
window directly (the compositor never sees a keybind or a `window.close`
event to hook), so the only way to get the effect there is to call the
dispatcher yourself before the shell actually terminates. Shadow `exit` with
a shell function that calls it through `hyprctl dispatch` and only falls back
to a real exit if the plugin isn't loaded:

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

The probe call deliberately fails an `assert` so its message — `type(hl.plugin)
.. "/" .. type(hl.plugin.glitchclose)` — comes back on stderr, since
`hyprctl dispatch` has no other way to return a value; that's how the function
detects whether the plugin is loaded before relying on it. The `return
hl.dsp.no_op()` in the second call exists only because
`hl.plugin.glitchclose.close()` itself returns nothing, which would otherwise
make `hyprctl dispatch` treat the call as an error. `glitchclose.close()`
closes the *currently focused* window, so this only does the right thing when
the terminal running `exit` is focused — true for the ordinary interactive
case this targets.

## Config

Defaults shown:

```lua
pcall(hl.config, {
    plugin = {
        ["3la_glitch_close"] = {
            enabled = 1,           -- 0 = off: windows close normally with no
                                   -- effect; glitchclose:close and
                                   -- hl.plugin.glitchclose.close() still close
                                   -- the focused window
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

The shader has a lot of dials, and closing a terminal every time you want to see
what one does gets old fast. [`3LA-GlitchClose-Viewer/`](3LA-GlitchClose-Viewer)
is a WebGL2 tuner that runs the plugin's actual shader over a still image: scrub
the close animation on a timeline, move the sliders, then hit **copy Lua** for a
ready-to-paste `pcall(hl.config, …)` block.

```sh
make -C 3LA-GlitchClose-Viewer open            # build the bundle and open it
make -C 3LA-GlitchClose-Viewer install-desktop # optional: add to the app launcher
./3LA-GlitchClose-Viewer/run.sh
```

It has no shader of its own. `sync.py` extracts the GLSL from `shader.hpp` and
the option list — keys, defaults, min/max — from `main.cpp`, so the controls are
whatever the plugin currently registers, and a config key with no matching
uniform is a build error rather than a dead slider. See
[3LA-GlitchClose-Viewer/README.md](3LA-GlitchClose-Viewer/README.md).

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

---

# 3LA-TitleBars

Draws a solid-color bar of `height` pixels above every window's top edge, with
the window's own title rendered on it. The space is *reserved*, like a real
titlebar: the window's own content is pushed down by `height + gap +
gap.bottom` and the bar sits above it rather than painting over the app's own
top pixels.

![3LA-TitleBars](assets/titlebars.png)

*A title bar reserved above a kitty, showing the window's own title.*

## Config

Defaults shown:

```lua
hl.config({ plugin = { ["3la_titlebars"] = {
    enabled = 1,             -- 0 = off: no bars and no reserved space, applied
                             -- live (windows snap back to their plain geometry)
    height = 24,             -- bar height (px)
    gap = 7,                 -- gap (px) above the bar
    ["gap.top"] = -1,        -- gap (px) above the bar (-1 = use gap)
    ["gap.bottom"] = 7,      -- gap (px) below the bar, between it and the window underneath

    ["col.active"] = "rgba(690005ff)",  -- focused-window bar color
    ["col.inactive"] = 0,               -- unfocused-window bar color.
                                        -- 0 = follow col.active

    ["opacity.active"] = 1.0,   -- opacity (0..1) of the bar, its text and its
    ["opacity.inactive"] = 1.0, -- shadow -- mirrors decoration:active_opacity/
                                -- inactive_opacity, since the bar otherwise
                                -- ignores the renderer's own opacity multiplier

    shadow = 1,                    -- soft shadow behind the bar (0 = off).
                                   -- also off whenever decoration:shadow:enabled
                                   -- is false -- see "Notes" below
    ["shadow.size"] = 20,          -- spread distance (px)
    ["shadow.strength"] = 0.5,     -- overall intensity (0..1)
    ["shadow.col"] = "rgba(000000aa)", -- translucent black

    ["text.size"] = 12,                     -- title font size (px)
    ["text.font"] = "",                     -- title font family (empty = follow misc:font_family)
    ["text.col.active"] = "rgba(ffffffff)", -- title text color, focused window
    ["text.col.inactive"] = 0,              -- title text color, unfocused window.
                                            -- 0 = follow text.col.active

    ignore_class = "",  -- regex of window classes to never give a title bar (empty = none)
    ignore_title = "",  -- regex of window titles to never give a title bar (empty = none)

    title_rules = "",   -- custom titles; see "Custom titles" below (empty = none)
} } })
```

## Custom titles

`title_rules` overrides the text shown on the bar without touching the
window's real title (used elsewhere for matching, alt-tab, etc.). It packs a
list of rules into one string, since a plugin-defined repeatable keyword (the
`windowrulev2`-style approach) has no path in from `hl.config`'s Lua bridge —
only typed config *values* do:

```lua
-- 'class_regex,title_regex,override text' rules, separated by ';'.
-- Either regex may be empty to match any class/title. First match wins.
title_rules = "^(firefox)$,,WEB BROWSER;^(kitty)$,^(btop)$,SYSTEM MONITOR",
```

A window whose class matches `^(firefox)$` (any title) shows "WEB BROWSER"; a
`kitty` window whose title matches `^(btop)$` shows "SYSTEM MONITOR"; every
other window keeps showing its own (still-uppercased) title. Only the first
two commas in a rule are treated as field separators, so the override text
itself may contain commas. All 3 fields are required — a common mistake is
writing a 2-field shorthand like `^(btop)$,SYSTEM MONITOR` meaning "match this
title", which is instead read as a *class* regex with no override text field
and gets skipped; leave the class field empty instead: `,^(btop)$,SYSTEM
MONITOR`. An unparseable rule (missing a field, or an invalid regex) is
skipped individually rather than breaking the whole list, and raises an
in-session notification naming the bad rule and why, rather than just quietly
never matching. Re-parsed only when the string actually changes, same as
`ignore_class` / `ignore_title`.

## Toggling a bar per-window

`3la_titlebars:toggle` flips the bar on the currently **focused** window only
(independent of `ignore_class`/`ignore_title`, and reversible — press again to
bring it back). Exposed two ways:

```lua
-- classic dispatch string (hyprland.conf-style bind, or hl.dsp.exec_cmd("hyprctl dispatch 3la_titlebars:toggle"))
bind = SUPER SHIFT, B, 3la_titlebars:toggle

-- Lua keybind (hl.plugin.titlebars only exists while the plugin is loaded, hence the pcall)
hl.bind("SUPER + SHIFT + B", function()
    pcall(function() hl.plugin.titlebars.toggle() end)
end, { description = "Toggle title bar on active window" })
```

`hyprctl dispatch 3la_titlebars:toggle` does **not** work as-is on a Lua-parsed
config (`hyprctl dispatch` there routes through `hl.dispatch(...)`, which
evaluates its argument as a Lua expression, not a dispatcher string) — use the
Lua function form above, or `hyprctl eval 'hl.plugin.titlebars.toggle()'` for
one-off testing.

## Notes

- The bar is *reserved* space: tiled windows shrink by `height + gap.top +
  gap.bottom` to make room above them, the same way 3LA-Corners reserves space
  for its brackets. The bar itself is inset by `gap.top` on top (`-1`, the
  default, reuses `gap`) and `gap.bottom` on the bottom within that reserved
  slot, so it floats with a margin instead of touching the window's own top
  edge or the reserved area's outer boundary.
- The bar is always exactly as wide as the window, flush with the outer edges
  of its border, whatever other decorations (e.g. 3LA-Corners) reserve around
  it.
- The title text is rendered to a texture and cached per window; it is only
  re-rendered when the title string, resolved color, font family, font size or
  available width actually change, and repainted immediately on a
  `window.title` event (e.g. a browser tab switch) rather than waiting for
  some unrelated redraw. The text is clipped/ellipsised to fit the bar's
  width minus an 8px padding on each side, and centred vertically; when the
  spare height is odd, the extra pixel goes below the text.
- The title is uppercased before rendering (ASCII letters only — accents and
  any icon glyphs in a window title pass through untouched rather than being
  mangled by a byte-wise transform of UTF-8).
- The window's own drop shadow hugs the core border, not the bar: Hyprland
  grows the shadow's box to include any decoration flagged
  `DECORATION_PART_OF_MAIN_WINDOW` (the core border carries it; that's how the
  shadow normally follows it seamlessly), and 3LA-TitleBars deliberately
  leaves that flag off so that shadow stays where it always was.
- The bar's own `shadow` is a *separate*, independent effect, not a second
  copy of the window's shadow — a plugin can't reach Hyprland's real shadow
  shader, so it's the same cheap layered-rect technique 3LA-Corners' `glow`
  already uses: `shadow.size` stacked, expanded, fading-alpha copies of the
  bar's box drawn behind it (6 layers, a stepped halo rather than a smooth
  blur). Sized and colored entirely on its own terms via `shadow.*` — except
  for on/off: turning off `decoration:shadow:enabled` also turns off the
  bar's shadow, so one global shadow toggle covers both. The plugin's own
  `shadow = 0` still lets you disable just the bar's shadow independently.
- Fullscreen windows never get a bar — the decoration bails out early for them,
  same as 3LA-Corners.
- Closing a window no longer leaves its bar stuck on screen — same fix as
  3LA-Corners: `window.close` damages it while geometry is still valid, and a
  client that exits on its own only ever reaches `window.destroy`, too late to
  recompute a box, so the decoration instead damages the whole output it was
  last drawn on.
- `ignore_class` / `ignore_title` opt specific windows out entirely (no bar,
  no reserved space, window keeps its plain geometry) — same regex-filter
  convention as 3LA-GlitchClose's options of the same name. Re-evaluated live
  on `window.title` / `window.class_` events and on config reload, so a window
  that changes class or title crossing the pattern gets its bar added or
  removed on the spot, not just on next open.
- Like 3LA-Corners, the bar (and its text) ignores the renderer's own opacity
  multiplier (window opacity rules, `decoration:active_opacity`/
  `inactive_opacity`): only the configured colors' own alpha, scaled by this
  plugin's own `opacity.active`/`opacity.inactive`, controls them. The shadow
  and text both fade along with the bar's own alpha.

---

# 3LA-Tint

Recolours the grey, black and white pixels of chosen windows toward one theme
colour, keeping each pixel's own lightness. Saturated pixels (images, video,
coloured UI) are left alone.

It's for the apps that don't take ricing: ones that ignore your GTK/Qt theme
or have no theming support at all, such as a browser, a web app or an Electron
tool. Tint can't restyle them, but it can pull their neutral backgrounds and
chrome toward the same colour the rest of your desktop uses (a matugen colour,
say), so they feel like part of your theme instead of a stock-grey window.

A window is tinted when its class **or** its title matches a regex. The tint
covers the window's subsurfaces and popups (menus, dropdowns) too.

![3LA-Tint](assets/tint.png)

*Same kitty, before (left) and after (right): the near-black background is
pulled toward `col.tint`. Shot at `strength = 1.0` so the shift is visible on a
dark terminal; at the default 0.3 it is subtle.*

## Example: theming Chrome from matugen

From a working `config/plugins.lua`. `col.tint` is a matugen global from
`config/colors.lua`, so a theme change re-tints on the next config reload;
`strength` is read from a per-scheme cache file with a 0.3 fallback:

```lua
-- 3LA-Tint: recolor grey/black/white pixels of matched windows toward col.tint
-- (hue + saturation used), remapping lightness into light_lo..light_hi.
-- Saturated pixels are left alone. Covers the window's subsurfaces and popups.
-- Repo: ~/git/Hypr-3LA/3LA-Tint (rebuild with `make` after Hyprland updates)
-- Colors come from config/colors.lua (matugen), so a theme change re-tints on
-- the next config reload.
-- strength is read from ~/.cache/matugen/tint-strength.lua, written by
-- ~/.config/hypr/scripts/set-tint-strength.sh (0.7 scheme-tonal-spot, else 0.3).
-- Callers: quickshell WallpaperDropdown.qml (after matugen) and
-- workspace40-HTB-windows.sh (apply_lime_theme / revert_theme). Fallback 0.3.
local ok, tint_strength = pcall(dofile, os.getenv("HOME") .. "/.cache/matugen/tint-strength.lua")
if not ok or type(tint_strength) ~= "number" then tint_strength = 0.3 end

pcall(hl.config, {
    plugin = {
        ["3la_tint"] = {
            enabled = 1,                    -- 0 = off
            -- Regexes of windows to tint; tinted if either matches. Read exact
            -- strings off `hyprctl clients`; a literal dot is \\. in Lua.
            match_title = "^(" .. table.concat({
                "Task Manager",
                --"Some Web App",
                "server1",
            }, "|") .. ")$",
            match_class = "^(" .. table.concat({
                --"cool-retro-term",
                "google-chrome",
                "chrome-claude.ai__new-Default",
            }, "|") .. ")$",
            ["col.tint"] = secondary_container, -- tint color (hue + saturation used)
            strength = tint_strength,       -- blend over greys (0..1); see header
            sat_lo = 0.1,                   -- chroma <= this: fully tinted
            sat_hi = 0.2,                   -- chroma >= this: untouched
            min_luma = 0.0,                 -- darker pixels stay untinted (0 = tint black too)
            light_lo = 0.06,                -- HSL lightness black maps to
            light_hi = 0.9,                 -- HSL lightness white maps to
        }
    }
})
```

Chrome web apps (`--app` windows, installed PWAs) get their own class, e.g.
`chrome-www.youtube.com__-Default`, so a `^(google-chrome)$` class match tints
normal browser windows but leaves such an app alone — handy for keeping video
untouched.

## Config

Defaults shown:

```lua
hl.config({ plugin = { ["3la_tint"] = {
    enabled = 1,              -- 0 = off
    match_title = "",         -- regex of window titles to tint (empty = none)
    match_class = "",         -- regex of window classes to tint (empty = none)
    ["col.tint"] = "rgba(2decececff)", -- only its hue and saturation are used
    strength = 1.0,           -- blend of the tint over greys (0..1)
    sat_lo = 0.06,            -- chroma at or below which a pixel is fully tinted
    sat_hi = 0.20,            -- chroma at or above which a pixel is left alone
    min_luma = 0.0,           -- pixels darker than this stay untinted (0 = tint black too)
    light_lo = 0.04,          -- HSL lightness pure black is remapped to
    light_hi = 0.90,          -- HSL lightness pure white is remapped to
} } })
```

How a pixel is recoloured:

- **Which pixels:** chroma (`max(rgb) - min(rgb)`) decides. At or below
  `sat_lo` the pixel is fully tinted, at or above `sat_hi` it is untouched, and
  in between it fades smoothly, so anti-aliased edges of coloured content don't
  band. `min_luma` optionally protects near-black pixels.
- **What colour:** the hue and saturation of `col.tint`, with the pixel's own HSL
  lightness remapped into `[light_lo, light_hi]`. Without the remap pure black
  and pure white would stay colourless whatever the tint; `light_lo`/`light_hi`
  pull them just far enough in to carry the hue.
- **How much:** `strength` blends the result over the original. Low values
  (0.2–0.4) give a subtle cast; 1.0 fully recolours the greys.

## How it renders

At `RENDER_PRE_WINDOW` for a matching window the plugin notes where that
window's draws start in Hyprland's render pass; at `RENDER_POST_WINDOW` it
collects the surface elements (main surface, subsurfaces, popups) the window
just queued and adds one custom pass element right after them. When the pass
runs, that element copies those boxes out of the framebuffer being drawn and
draws them back through the tint shader. Anything drawn later — other windows,
layers, the cursor — stays on top and untinted.

It is built to cost nothing when nothing changes:

- **damage-only:** each frame only the damaged part of a box is copied and
  re-drawn, so a blinking caret costs a caret-sized pass, not a window-sized
  one, and no pixel is tinted twice across frames. A few scattered damage rects
  are copied separately rather than as one spanning rectangle.
- **cached matching:** `std::regex` is slow, so the match result is memoised
  per window and only re-evaluated when its class, title or a pattern changes.
- **cheap shader:** the tint's hue/saturation are computed once on the CPU, and
  the rounded-corner maths only runs for pixels actually in a corner.

## Notes

- **Menu shadows aren't tinted.** A popup's box includes its translucent drop
  shadow, and tinting the composited framebuffer there would tint whatever lies
  behind the menu, leaving a tinted halo. The shader reads the popup's own
  buffer alpha and only tints where the surface is at least half opaque. The
  check is skipped for surfaces the client declares fully opaque (Chrome's main
  window), and works on rotated monitors too.
- **Decorations drawn over the window stay untinted.** The tint pass for the
  window's own surfaces is queued right after them, before the decorations
  Hyprland draws above the window (3LA-MilSpec's corner covers and title bar,
  3LA-Corners, 3LA-TitleBars), so a MilSpec cover keeps exactly the colour of
  the frame fill around it. Popups get their own tint pass after the popups
  themselves.
- Rounded window corners are respected: the main surface's rounding and
  rounding power are passed to the shader, so the pixels outside the corner
  curve are left alone.
- A window mid-animation (open/close/move with a transform) is drawn into a
  separate pass, so the tint skips those frames and returns when it settles.
- `col.tint` changes take effect on the next config reload, which also damages
  every monitor so the whole window re-tints at once.
- Requires the GL renderer; on a Vulkan backend it notifies and does nothing.
- `match_class` / `match_title` are C++ `std::regex`, **not** Lua patterns. An
  invalid regex matches nothing rather than erroring.

