# Hypr-3LA

Hyprland plugins that give a tiling desktop a CCTV / surveillance-rig
aesthetic: chamfered HUD frames, targeting-reticle corner brackets, reserved
title bars, a GLSL "signal lost" window-close effect, and a shader that pulls
un-themeable apps toward your theme colour.

All are C++ Hyprland plugins built against **Hyprland 0.56.2**, configured
either through classic `hyprland.conf` keywords or Hyprland's Lua config
(`hl.config`), and installable with `hyprpm`. Every plugin has an `enabled`
option that turns it off live without unloading it.

| plugin | what it does | docs |
|---|---|---|
| [3LA-MilSpec](3LA-MilSpec) | chamfered HUD-panel frame around every window, optional title bar inside it | [README](3LA-MilSpec/README.md) |
| [3LA-Corners](3LA-Corners) | targeting-reticle corner brackets outside every window, with flash and glow | [README](3LA-Corners/README.md) |
| [3LA-GlitchClose](3LA-GlitchClose) | GLSL "signal lost" collapse when a window closes | [README](3LA-GlitchClose/README.md) |
| [3LA-GlitchClose-Viewer](3LA-GlitchClose-Viewer) | WebGL2 tuner for the GlitchClose shader (not a plugin) | [README](3LA-GlitchClose-Viewer/README.md) |
| [3LA-TitleBars](3LA-TitleBars) | solid-colour title bar reserved above every window | [README](3LA-TitleBars/README.md) |
| [3LA-Tint](3LA-Tint) | recolours the greys/blacks/whites of chosen windows toward a theme colour | [README](3LA-Tint/README.md) |

## Plugins

### 3LA-MilSpec

[![3LA-MilSpec: chamfered frame with title bar](assets/milspec.png)](3LA-MilSpec/README.md)

Wraps every window in a stroked frame with the **top-left and bottom-right
corners cut at 45°** — a "mil-spec" HUD panel outline. Optionally reserves a
title bar *inside* the frame showing the window's title.

- Per-side title-bar insets, active/inactive colours and opacity, and a
  per-window toggle (`3la_milspec:toggle`)
- Background fill between frame and window, drop shadow that follows the
  chamfers, and corner covers that make the app itself look cut
- Per-window opt-outs for the whole frame, just the bar, just the stroke or
  just the shadow
- Custom bar titles (`title_rules`), same format as 3LA-TitleBars
- Drawn with cairo into one cached, antialiased texture per window

→ [Full documentation](3LA-MilSpec/README.md)

### 3LA-Corners

[![3LA-Corners](assets/corners.png)](3LA-Corners/README.md)

Draws two-segment corner brackets outside every window's border, like a
targeting reticle. The space is reserved, so brackets never overlap
neighbouring windows.

- Separate focused / unfocused colours, decoupled from window opacity
- Flash bursts on window spawn and (optionally) on focus change
- Soft glow during a flash, and optional lines joining the brackets into a
  full frame
- Automatically frames 3LA-TitleBars' bar when both are loaded

→ [Full documentation](3LA-Corners/README.md)

### 3LA-GlitchClose

[![3LA-GlitchClose](assets/glitchclose-showcase.gif)](3LA-GlitchClose/README.md)

Plays a CCTV "signal lost" collapse over a window as it closes: the content
tears apart, static and a backdrop ramp in, a `SIGNAL LOST` caption appears,
and only then does the window actually close.

- One GLSL fragment-shader pass per frame: v-sync seam, slice tearing,
  macroblock corruption, ghost copies, chromatic aberration, scanlines, static
- `glitchclose:close` dispatcher plays the effect over the still-open window
  before sending the real close
- Every knob is a live uniform — re-theme a running effect with no reload
- The window's border is never glitched
- Tunable visually with [3LA-GlitchClose-Viewer](3LA-GlitchClose-Viewer/README.md)
- Requires the GL renderer

→ [Full documentation](3LA-GlitchClose/README.md)

### 3LA-TitleBars

[![3LA-TitleBars](assets/titlebars.png)](3LA-TitleBars/README.md)

Reserves a solid-colour bar above every window's top edge, with the window's
title drawn on it. The window's content is pushed down rather than painted
over, like a real title bar.

- Active/inactive bar and text colours, opacity and font
- Independent bar shadow
- Custom titles per class/title regex (`title_rules`)
- Per-window toggle (`3la_titlebars:toggle`) and regex opt-outs

→ [Full documentation](3LA-TitleBars/README.md)

### 3LA-Tint

[![3LA-Tint: before and after](assets/tint.png)](3LA-Tint/README.md)

Built mainly for Google Chrome and GTK apps that don't follow your desktop
theme (and anything else with little or no theming): recolours their grey,
black and white pixels toward one theme colour, keeping each pixel's
lightness. Saturated pixels — images, video,
coloured UI — are left alone.

- Windows chosen by class or title regex; covers subsurfaces and popups
- Smooth chroma-based falloff, so anti-aliased edges don't band
- Damage-only rendering and memoised matching — near-zero cost when idle
- Requires the GL renderer

→ [Full documentation](3LA-Tint/README.md)

## Demo

Three kitty terminals framed by 3LA-Corners, closed one by one through
`glitchclose:close`. Setup runs at 2×; the closes are real time:

![demo: three terminals closed with the shader glitch collapse](assets/glitchclose-demo.gif)

([full-quality mp4](assets/glitchclose-demo.mp4))

3LA-TitleBars on a live desktop, switching between the tiled layout and a
fullscreen workspace, with some windows relabelled by `title_rules`:

![demo: 3LA-TitleBars on a live desktop, switching workspaces](assets/titlebars-demo.gif)

([full-quality mp4](assets/titlebars-demo.mp4))

## Requirements

### To run

| | |
|---|---|
| **Hyprland 0.56.2** | Matched on *commit hash*, not version string, so two 0.56.2 builds from different commits still mismatch — see [Rebuilding after a Hyprland update](#rebuilding-after-a-hyprland-update) |
| **A GL renderer** | 3LA-GlitchClose and 3LA-Tint need raw GL calls; on a Vulkan backend they raise a notification and disable themselves. 3LA-Corners and 3LA-TitleBars are unaffected. 3LA-MilSpec only uses the renderer's texture API, but has only been tested on GL. |

Nothing else. The built `.so` links only the C++ runtime — every Hyprland and
GL symbol is left undefined and resolves against the running compositor at
`dlopen`. The one exception is 3LA-MilSpec, which links `libcairo`; Hyprland
itself depends on cairo, so it is already installed.

### To build

On Arch, everything comes from the official repos:

```sh
sudo pacman -S --needed hyprland gcc make binutils pkgconf
```

| package | provides | why |
|---|---|---|
| `hyprland` | `/usr/include/hyprland`, `hyprland.pc`, `hyprpm` | the headers the plugins compile against |
| `gcc` | `g++` | the Makefiles ask for `-std=c++26` |
| `make` | `make` | build driver |
| `binutils` | `strip` | release builds are stripped |
| `pkgconf` | `pkg-config` | resolves the include paths |

There is no separate `-devel` split on Arch: the headers ship with the
compositor. **On other distributions** you need the package carrying the
Hyprland headers and `hyprland.pc` (often `hyprland-devel`), or a source build
with headers installed. Verify before building:

```sh
pkg-config --modversion hyprland          # must equal your running compositor
pkg-config --cflags hyprland pixman-1 libdrm
hyprctl version | head -3
```

### Optional

| what | Arch package | needed for |
|---|---|---|
| `python3` | `python` | regenerating the shader tuner's bundle |
| `glslangValidator` | `glslang` | `make -C 3LA-GlitchClose-Viewer check` |
| `update-desktop-database` | `desktop-file-utils` | `make -C 3LA-GlitchClose-Viewer install-desktop` |
| a WebGL2 browser | `chromium` / `firefox` / … | opening the tuner (GLSL ES 3.00) |
| `cmake`, `meson`, `ninja`, `git` | same names | only for the `hyprpm` route |

## Building and installing

Two routes. **hyprpm** is less to remember and handles rebuilds; **manual** is
better if you are editing the plugins, since it skips the header rebuild.

### Option A — hyprpm

`hyprpm` reads [`hyprpm.toml`](hyprpm.toml), which declares all five plugins.

```sh
hyprpm update                                     # build headers for your Hyprland
hyprpm add https://github.com/ne0tt/Hypr-3LA
hyprpm enable 3LA-MilSpec
hyprpm enable 3LA-Corners
hyprpm enable 3LA-GlitchClose
hyprpm enable 3LA-TitleBars
hyprpm enable 3LA-Tint
hyprpm list                                       # confirm enabled: true
```

`hyprpm update` clones and configures Hyprland to produce its own header set,
so it needs `cmake`, `meson`, `ninja` and `git`, and takes a few minutes the
first time. Load at every startup — in `hyprland.conf`:

```ini
exec-once = hyprpm reload -n
```

or in `hyprland.lua`, inside your `hyprland.start` handler:

```lua
hl.exec_cmd("hyprpm reload -n")
```

The `commit_pins` entry in `hyprpm.toml` pairs a Hyprland commit with a
plugin-repo commit. If you fork this repo, bump the *second* hash after
committing or `hyprpm update` will keep fetching the older revision.

### Option B — build from source

```sh
git clone https://github.com/ne0tt/Hypr-3LA
cd Hypr-3LA

make -C 3LA-MilSpec
make -C 3LA-Corners
make -C 3LA-GlitchClose
make -C 3LA-TitleBars
make -C 3LA-Tint
```

Each plugin directory is self-contained, with no build order between them.
Targets, identical across every plugin:

| target | effect |
|---|---|
| `make` / `make all` | optimised, stripped `.so` |
| `make debug` | adds `-g`; forces a clean rebuild first |
| `make clean` | removes the `.so` |

Load into the running compositor (`hyprctl plugin load` needs an **absolute**
path):

```sh
hyprctl plugin load "$PWD/3LA-MilSpec/3LA-MilSpec.so"
hyprctl plugin load "$PWD/3LA-Corners/3LA-Corners.so"
hyprctl plugin load "$PWD/3LA-GlitchClose/3LA-GlitchClose.so"
hyprctl plugin load "$PWD/3LA-TitleBars/3LA-TitleBars.so"
hyprctl plugin load "$PWD/3LA-Tint/3LA-Tint.so"
```

### Loading at startup (Lua config)

Replace `/path/to/Hypr-3LA` with wherever you cloned the repo.

```lua
local HYPR3LA = "/path/to/Hypr-3LA"

hl.plugin.load(HYPR3LA .. "/3LA-Corners/3LA-Corners.so")
hl.plugin.load(HYPR3LA .. "/3LA-GlitchClose/3LA-GlitchClose.so")
hl.plugin.load(HYPR3LA .. "/3LA-TitleBars/3LA-TitleBars.so")
hl.plugin.load(HYPR3LA .. "/3LA-Tint/3LA-Tint.so")
hl.plugin.load(HYPR3LA .. "/3LA-MilSpec/3LA-MilSpec.so")

hl.on("hyprland.start", function()
    hl.exec_cmd(HYPR3LA .. "/3LA-Corners/autoload.sh")
    hl.exec_cmd(HYPR3LA .. "/3LA-GlitchClose/autoload.sh")
    -- ...one per plugin you use
end)

require("config.colors")  -- e.g. matugen-generated colour globals
require("config.plugins") -- plugin settings (see each plugin's README)
```

Each plugin ships an `autoload.sh` as a startup fallback: it loads the `.so`
via `hyprctl` only if it is not already present, then re-applies settings from
`~/.config/hypr/config/plugins.lua` (adapt the path to your layout). It is safe
to run repeatedly.

Re-apply settings without restarting Hyprland:

```sh
hyprctl eval 'dofile(os.getenv("HOME") .. "/.config/hypr/config/plugins.lua")'
```

### Verifying the install

```sh
hyprpm list                                        # hyprpm route
hyprctl plugin list                                # either route
hyprctl getoption plugin:3la_glitch_close:duration # a registered option proves init ran
```

### Rebuilding after a Hyprland update

**All plugins must be rebuilt every time Hyprland updates.** The API commit
hash is baked in at compile time and compared against the running
compositor's at load. A mismatch refuses to load with a notification instead
of crashing the compositor.

```sh
# hyprpm route
hyprpm update && hyprpm reload

# manual route: clean + rebuild every plugin; --reload also unloads/reloads
# any that are currently running
./rebuild-plugins.sh --reload

# ...or one plugin by hand (unload before load — loading over a loaded
# plugin of the same name is refused)
make -C 3LA-GlitchClose clean && make -C 3LA-GlitchClose
hyprctl plugin unload "$PWD/3LA-GlitchClose/3LA-GlitchClose.so"
hyprctl plugin load   "$PWD/3LA-GlitchClose/3LA-GlitchClose.so"
```

### Troubleshooting

| symptom | cause | fix |
|---|---|---|
| `hyprland.pc not found` | headers missing or off `PKG_CONFIG_PATH` | install the Hyprland headers; check `pkg-config --modversion hyprland` |
| Notification: *version mismatch* | built against different headers than the running compositor | rebuild — see above |
| Notification: *requires the GL renderer* | Hyprland is on the Vulkan backend | 3LA-GlitchClose and 3LA-Tint cannot run there |
| `hyprctl plugin load` returns an error | relative path, or already loaded | use an absolute path; `hyprctl plugin unload` first |
| `keyword can't work with non-legacy parsers` | `hyprctl keyword` against the Lua config | use `hyprctl eval 'hl.config{...}'` |
| `unknown config key 'plugin.…'` | plugin not loaded, or option never registered | check `hyprctl plugin list`; guard the block with `hl.get_config` (below) |
| Builds fine, no visible effect | settings never applied | re-run your `plugins.lua` via `hyprctl eval 'dofile(…)'` |
| Plugin loads but `no such option` | an option was created but never passed to `HyprlandAPI::addConfigValueV2` | (development) register the option |

## Configuration

Settings are applied with `hl.config`, wrapped in `pcall` so config for a
not-yet-loaded plugin can't break the rest of your config:

```lua
pcall(hl.config, { plugin = {
    ["3la_milspec"]      = { offset = 10, thickness = 2, chamfer = 15 },
    ["3la_corners"]      = { offset = 10, length = 40, thickness = 1 },
    ["3la_glitch_close"] = { duration = 700, strength = 1.0, text = "SIGNAL LOST" },
    ["3la_titlebars"]    = { height = 24, ["gap.top"] = 7, ["gap.bottom"] = 7 },
    ["3la_tint"]         = { match_class = "^(google-chrome)$", strength = 0.3 },
} })
```

Classic `hyprland.conf` keywords work too, with the same option names:
`plugin { 3la_glitch_close { duration = 700 } }`.

Live-tweak without reloading (`hyprctl keyword` does not work with the Lua
parser):

```sh
hyprctl eval 'hl.config({ plugin = { ["3la_glitch_close"] = { strength = 2.0 } } })'
hyprctl getoption plugin:3la_glitch_close:duration
```

`pcall` stops a Lua error, but Hyprland still reports `unknown config key`
**per key** for a plugin that isn't loaded. Guard optional plugins with
`hl.get_config`, which returns `nil` for an unregistered option:

```lua
if hl.get_config("plugin:3la_milspec:enabled") ~= nil then
    hl.config({ plugin = { ["3la_milspec"] = { chamfer = 15 } } })
end
```

Notes:

- Prefer plain `hyprctl getoption` for regex options: `-j` prints backslashes
  unescaped, so the JSON fails to parse.
- All regex options are C++ `std::regex` (ECMAScript), **not** Lua patterns.
  A literal dot is `\\.` inside a Lua string.
- The plugins read their colours from config every frame, so feeding
  matugen-generated globals into the `col.*` options re-themes live.

Full option references, defaults and example configs are in each plugin's
README.

## Using plugins together

| combination | result |
|---|---|
| Corners + TitleBars | works well: the top brackets frame the bar |
| MilSpec + TitleBars | works, but gives two title bars — pick one |
| MilSpec + Corners | not recommended: Corners' top brackets collide with the frame's top edge |
| Tint + any decoration | decorations drawn over the window stay untinted |
| GlitchClose + anything | independent |

One `title_rules` string can drive both 3LA-TitleBars and 3LA-MilSpec, and one
keybind can toggle both plugins' bars — see their READMEs.

## Repository layout

```
3LA-MilSpec/              chamfered mil-spec frame (+ optional title bar)
3LA-Corners/              corner brackets decoration
3LA-GlitchClose/          GLSL shader signal-loss collapse
3LA-GlitchClose-Viewer/   WebGL tuner for the shader above
3LA-TitleBars/            solid-colour title bar
3LA-Tint/                 GLSL recolour of a window's grey pixels
assets/                   screenshots and demo recordings
hyprpm.toml               plugin manifest + Hyprland/plugin commit pins
rebuild-plugins.sh        clean-rebuild (and optionally reload) every plugin
```
