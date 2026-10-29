# 3LA-Tint

Recolours the grey, black and white pixels of chosen windows toward one theme
colour, keeping each pixel's relative lightness. Saturated pixels (images,
video, coloured UI) are left alone.

It's aimed mainly at **Google Chrome** and **GTK apps** that don't follow the
rest of the desktop's theme. Chrome draws its own UI and web pages bring their
own colours, and many GTK apps (libadwaita ones especially) ignore a custom
GTK theme or only offer stock light/dark. The same goes for any app with
little or no theming support, such as a web app or an Electron tool. Tint
can't restyle them, but it can pull their neutral backgrounds and chrome
toward the colour the rest of your desktop uses (a matugen colour, say), so
they feel like part of your theme instead of a stock-grey window.

A window is tinted when its class **or** its title matches a regex. The tint
covers the window's subsurfaces and popups (menus, dropdowns) too.

![3LA-Tint](../assets/tint.png)

*Same kitty, before (left) and after (right): the near-black background is
pulled toward `col.tint`. Shot at `strength = 1.0` so the shift is visible on
a dark terminal; at 0.3 it is subtle.*

Part of [Hypr-3LA](../README.md).

## Contents

- [Build and load](#build-and-load)
- [Config](#config)
- [Example: theming Chrome from matugen](#example-theming-chrome-from-matugen)
- [How it renders](#how-it-renders)
- [Notes](#notes)

## Build and load

Requires Hyprland 0.57.0 headers (see the [main README](../README.md#requirements))
and the **GL renderer** (the only one Hyprland 0.57.0 creates). Tint draws
with raw GL; under any other renderer it shows a notification the first time a
matched window is drawn, and tints nothing.

```sh
make                                              # → 3LA-Tint.so
hyprctl plugin load "$PWD/3LA-Tint.so"            # absolute path required
```

At startup, load it from `hyprland.lua` with
`hl.plugin.load("/path/to/Hypr-3LA/3LA-Tint/3LA-Tint.so")`. `autoload.sh` is
an optional startup fallback; see
[Loading at startup](../README.md#loading-at-startup-lua-config).

Or via hyprpm: `hyprpm enable 3LA-Tint` (after `hyprpm add`, see
[Option A — hyprpm](../README.md#option-a--hyprpm)).

Rebuild after every Hyprland update: `make clean && make` (plain `make` only
tracks the plugin's own sources), or `./rebuild-plugins.sh` at the repo root
for every plugin; a failed build keeps the previous `.so`. A `.so` built
against a different Hyprland refuses to load, with a notification.

## Config

Defaults shown:

```lua
hl.config({ plugin = { ["3la_tint"] = {
    enabled = 1,              -- 0 = off
    match_title = "",         -- regex of window titles to tint (empty = none)
    match_class = "",         -- regex of window classes to tint (empty = none)
    exclude_class = "",       -- regex of classes never tinted, even if matched
    exclude_title = "",       -- regex of titles never tinted, even if matched
    fullscreen_exclude_class = "", -- regex of classes left untinted while
                                   -- fullscreen or maximized (empty = none)
    ["col.tint"] = "rgba(2dececff)", -- colour string; only its hue and saturation are used
    strength = 1.0,           -- blend of the tint over greys
    sat_lo = 0.06,            -- chroma at or below which a pixel is fully tinted
    sat_hi = 0.20,            -- chroma at or above which a pixel is left alone
    min_luma = 0.0,           -- pixels darker than this stay untinted (0 = tint black too)
    light_lo = 0.04,          -- HSL lightness pure black is remapped to
    light_hi = 0.90,          -- HSL lightness pure white is remapped to
} } })
```

Every float option is `0..1`; a value outside that is rejected as a config
error. `light_hi` below `light_lo` is treated as `light_lo`, and `sat_hi` is
kept at least 0.001 above `sat_lo`.

How a pixel is recoloured:

- **Which pixels:** chroma (`max(rgb) - min(rgb)`) decides. At or below
  `sat_lo` the pixel is fully tinted, at or above `sat_hi` it is untouched,
  and in between it fades smoothly, so anti-aliased edges of coloured content
  don't band. `min_luma` optionally protects near-black pixels: a pixel whose
  Rec. 709 luma is below `min_luma - 0.02` is untouched, fading in up to
  `min_luma`; 0 turns the check off.
- **What colour:** the hue and saturation of `col.tint`, with the pixel's own
  HSL lightness remapped into `[light_lo, light_hi]`. Without the remap pure
  black and pure white would stay colourless whatever the tint.
- **How much:** `strength` blends the result over the original. Low values
  (0.2–0.4) give a subtle cast; 1.0 fully recolours the greys. The pixel's
  alpha is kept; `col.tint`'s alpha is ignored.

**Exclusions win over matches.** A window is tinted when `match_class` or
`match_title` matches **and** neither `exclude_class` nor `exclude_title`
does. `fullscreen_exclude_class` adds a conditional exclusion: a matching
window is tinted normally, but left alone while it is fullscreen or
maximized — e.g. a browser that should blend in while tiled but show video
in its true colours when fullscreened. The verdict is re-evaluated when the
window's fullscreen state changes.

Read exact class/title strings off `hyprctl clients`. All five regex options
are C++ `std::regex` (ECMAScript), **not** Lua patterns, and are searched
rather than anchored: `chrome` matches any class containing it, so use
`^(...)$` for an exact match. An empty pattern matches nothing. A literal dot
is `\\.` in a Lua string.

## Example: theming Chrome from matugen

`col.tint` is a matugen colour global (whatever your colour template
defines), so a theme change re-tints on the next config reload. `strength` is
optionally read from a cache file a theme-switch script can write, with a 0.3
fallback:

```lua
local ok, tint_strength = pcall(dofile, os.getenv("HOME") .. "/.cache/tint-strength.lua")
if not ok or type(tint_strength) ~= "number" then tint_strength = 0.3 end

pcall(hl.config, {
    plugin = {
        ["3la_tint"] = {
            enabled = 1,
            -- tinted if either regex matches
            match_title = "^(Task Manager)$",
            match_class = "^(" .. table.concat({
                "google-chrome",
                "chromium",
            }, "|") .. ")$",
            ["col.tint"] = secondary_container, -- hue + saturation used
            strength = tint_strength,
            sat_lo = 0.1,
            sat_hi = 0.2,
            min_luma = 0.0,
            light_lo = 0.06,
            light_hi = 0.9,
        }
    }
})
```

Chrome web apps (`--app` windows, installed PWAs) get their own class, e.g.
`chrome-www.youtube.com__-Default`, so a `^(google-chrome)$` class match tints
normal browser windows but leaves such an app alone. If your `match_class` is
broader, keep video apps untouched with the exclusions:

```lua
pcall(hl.config, {
    plugin = {
        ["3la_tint"] = {
            -- never tinted, even if matched; a literal dot is \\. in Lua
            exclude_class = "^(" .. table.concat({
                "chrome-www\\.youtube\\.com__-Default",
            }, "|") .. ")$",
            -- untinted only while fullscreen or maximized
            fullscreen_exclude_class = "^(google-chrome)$",
        }
    }
})
```

`pcall` doesn't suppress Hyprland's "unknown config key" error when 3LA-Tint
isn't loaded. To keep the config loading cleanly without it, guard a block on
one of its keys being registered (`hl.get_config` returns `nil` otherwise):

```lua
if hl.get_config("plugin:3la_tint:exclude_class") ~= nil then
    -- the pcall(hl.config, ...) block above
end
```

## How it renders

At `RENDER_PRE_WINDOW` for a matching window the plugin notes where that
window's draws start in Hyprland's render pass; at `RENDER_POST_WINDOW` it
collects the surface elements (main surface, subsurfaces, popups) the window
just queued and adds a custom pass element right after the window's own
surfaces, plus a second one after everything else the window queued for its
popups. When the pass runs, each element copies its boxes out of the
framebuffer being drawn and draws them back through the tint shader. Anything
drawn later — other windows, layers, the cursor — stays on top and untinted.

Boxes and damage are used in the framebuffer's own orientation. The pass
renders in logical orientation and Hyprland rotates the result for the panel
afterwards, so on a rotated monitor nothing is remapped; only when
`ctx.m_data.transformDamage` marks a panel-oriented target are they mapped
through the inverse monitor transform. Scissoring uses the same flag, as
Hyprland's own elements do.

It is built to cost nothing when nothing changes:

- **damage-only:** each frame only the damaged part of a box is copied and
  re-drawn, so a blinking caret costs a caret-sized pass, and no pixel is
  tinted twice across frames. Two to four scattered damage rects are copied
  separately; more are copied as their bounding box.
- **cached matching:** the regex match result is memoised per window and only
  re-evaluated when its class, title, fullscreen state or a pattern changes.
- **cheap shader:** the tint's hue/saturation are computed on the CPU only
  when `col.tint` changes, and the rounded-corner maths only runs for pixels
  actually in a corner.

## Notes

- **Translucent shadows aren't tinted.** For a surface with an alpha channel
  the shader reads the surface's own buffer alpha: nothing below 0.5 is
  tinted, fading in to full strength at 1.0, so popup drop shadows and
  client-side window shadows don't leave a tinted halo. The check is skipped
  for surfaces the client declares fully opaque, buffers without alpha, and
  buffers with a buffer transform.
- **Decorations drawn over the window stay untinted.** The tint pass is queued
  right after the window's own surfaces, before 3LA-MilSpec's covers and title
  bar, 3LA-Corners and 3LA-TitleBars, so a MilSpec corner cover keeps exactly
  the colour of the frame fill around it. Popups get their own tint pass.
- **Only pixels the surface painted are tinted.** Each box is clipped to the
  same region the renderer scissored that surface with, so where 3LA-MilSpec
  cuts a window's corner past its chamfer (or a client sets a
  `hyprland_surface_v1` visible region) the wallpaper and shadow showing
  through are left alone.
- Rounded window corners are respected (the main surface's rounding and
  rounding power are passed to the shader).
- While a window has an active transformer (`decoration:motion_blur` or
  `decoration:wobble` while it moves or resizes, or a plugin's), Hyprland
  draws it into a separate pass, so the tint skips those frames and returns
  when it settles.
- Options are read every frame, but only damaged pixels are redrawn. A config
  reload (`hyprctl reload`) damages every monitor, so every matched window
  re-tints at once; a change made with `hyprctl eval 'hl.config(...)'` fires
  no reload and shows up only as each region next redraws.
- An invalid `match_*` regex matches nothing, and an invalid `exclude_*` or
  `fullscreen_exclude_class` regex excludes nothing, rather than erroring.
