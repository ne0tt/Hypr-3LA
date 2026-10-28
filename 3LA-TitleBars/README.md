# 3LA-TitleBars

Draws a solid-colour bar of `height` pixels above every window's top edge,
with the window's own title rendered on it. The space is *reserved*, like a
real title bar: the window's content is pushed down by
`height + gap.top + gap.bottom` and the bar sits above it rather than
painting over the app's own top pixels.

![3LA-TitleBars](../assets/titlebars.png)

*A title bar reserved above a kitty, showing the window's own title.*

![demo: 3LA-TitleBars on a live desktop, switching workspaces](../assets/titlebars-demo.gif)

*On a live desktop, with some windows relabelled by `title_rules`.
([mp4](../assets/titlebars-demo.mp4))*

Part of [Hypr-3LA](../README.md).

## Contents

- [Build and load](#build-and-load)
- [Config](#config)
- [Custom titles](#custom-titles)
- [Toggling a bar per-window](#toggling-a-bar-per-window)
- [Example config](#example-config)
- [Notes](#notes)

## Build and load

Requires Hyprland 0.57.0 headers (see the [main README](../README.md#requirements)).
Works on both the GL and Vulkan renderers.

```sh
make                                              # → 3LA-TitleBars.so
hyprctl plugin load "$PWD/3LA-TitleBars.so"       # absolute path required
```

Or via hyprpm: `hyprpm enable 3LA-TitleBars`. Rebuild after every Hyprland
update.

## Config

Defaults shown:

```lua
hl.config({ plugin = { ["3la_titlebars"] = {
    enabled = 1,             -- 0 = off: no bars and no reserved space, applied
                             -- live (windows snap back to their plain geometry)
    height = 24,             -- bar height (px)
    gap = 7,                 -- gap (px) above the bar
    ["gap.top"] = -1,        -- gap (px) above the bar (-1 = use gap)
    ["gap.bottom"] = 7,      -- gap (px) between the bar and the window underneath

    ["col.active"] = "rgba(690005ff)",  -- focused-window bar color
    ["col.inactive"] = 0,               -- unfocused-window bar color.
                                        -- 0 = follow col.active

    ["opacity.active"] = 1.0,   -- opacity (0..1) of the bar, its text and its
    ["opacity.inactive"] = 1.0, -- shadow -- mirrors decoration:active_opacity/
                                -- inactive_opacity, since the bar otherwise
                                -- ignores the renderer's own opacity multiplier

    shadow = 1,                    -- soft shadow behind the bar (0 = off).
                                   -- also off whenever decoration:shadow:enabled
                                   -- is false
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

Live-tweak and inspect:

```sh
hyprctl eval 'hl.config({ plugin = { ["3la_titlebars"] = { height = 28 } } })'
hyprctl getoption plugin:3la_titlebars:height
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
other window keeps its own (uppercased) title.

- **All 3 fields are required.** Only the first two commas split fields, so
  the override text may contain commas. A 2-field shorthand like
  `^(btop)$,SYSTEM MONITOR` is read as a *class* regex with no override and
  skipped; leave the class empty instead: `,^(btop)$,SYSTEM MONITOR`.
- **Regexes are searches**, not full matches — anchor with `^...$`.
- **Bad rules are skipped individually** (missing field, invalid regex) with
  an in-session notification naming the rule and why.
- Re-parsed only when the string changes.

The same format is used by 3LA-MilSpec's
[`title_rules`](../3LA-MilSpec/README.md#custom-bar-titles), so one string can
drive both.

## Toggling a bar per-window

`3la_titlebars:toggle` flips the bar on the currently **focused** window only
(independent of `ignore_class` / `ignore_title`, reversible). Exposed two
ways:

```lua
-- classic dispatch string (hyprland.conf-style bind)
bind = SUPER SHIFT, B, 3la_titlebars:toggle

-- Lua keybind (hl.plugin.titlebars only exists while the plugin is loaded, hence the pcall)
hl.bind("SUPER + SHIFT + B", function()
    pcall(function() hl.plugin.titlebars.toggle() end)
end, { description = "Toggle title bar on active window" })
```

`hyprctl dispatch 3la_titlebars:toggle` does **not** work on a Lua-parsed
config (`hyprctl dispatch` evaluates its argument as Lua) — use
`hyprctl eval 'hl.plugin.titlebars.toggle()'` for one-off testing.

## Example config

Themed from matugen colour globals (`primary`, `secondary_container`,
`shadow` — whatever your colour template defines), with a shared
`title_rules` string and some ignore lists:

```lua
local title_rules = table.concat({
    ",^(btop)$,SYSTEM MONITOR",
    "^(org.gnome.Nautilus)$,,FILE SYSTEM EXPLORER",
}, ";")

pcall(hl.config, {
    plugin = {
        ["3la_titlebars"] = {
            enabled = 1,
            height = 24,
            ["gap.top"] = 8,
            ["gap.bottom"] = 3,
            ["opacity.active"] = 1.0,
            ["opacity.inactive"] = 0.85,
            ["col.active"] = primary,
            ["col.inactive"] = secondary_container,
            shadow = 1,
            ["shadow.size"] = 9,
            ["shadow.strength"] = 0.5,
            ["shadow.col"] = "rgba(000000ee)",
            ["text.size"] = 16,
            ["text.font"] = "Bebas Neue",      -- fontconfig family (fc-list)
            ["text.col.active"] = on_primary,
            ["text.col.inactive"] = on_secondary_container,
            -- regex of window classes that never get a bar; a literal dot is \\. in Lua
            ignore_class = "^(" .. table.concat({
                "code",
                "chrome-www\\.youtube\\.com__-Default",
            }, "|") .. ")$",
            ignore_title = "",
            title_rules = title_rules,
        }
    }
})
```

## Notes

- The bar is *reserved* space: tiled windows shrink by
  `height + gap.top + gap.bottom`. The bar is inset by `gap.top` above and
  `gap.bottom` below within that slot, so it floats with a margin.
- The bar is always exactly as wide as the window, flush with the outer edges
  of its border, whatever other decorations reserve around it.
- The title text is rendered to a texture and cached per window; it is only
  re-rendered when the title, colour, font, size or available width change,
  and repainted immediately on a `window.title` event. Text is ellipsised to
  fit the bar minus 8 px padding each side, and vertically centred.
- The title is uppercased (ASCII letters only — accents and icon glyphs pass
  through untouched).
- The window's own drop shadow hugs the core border, not the bar: the
  decoration deliberately leaves `DECORATION_PART_OF_MAIN_WINDOW` off.
- The bar's own `shadow` is a separate effect — a plugin can't reach
  Hyprland's shadow shader, so it is 6 stacked, expanded, fading-alpha copies
  of the bar's box (a stepped halo, not a smooth blur). Turning off
  `decoration:shadow:enabled` also turns off the bar's shadow; `shadow = 0`
  disables just the bar's.
- Fullscreen windows never get a bar.
- Closing a window never leaves its bar on screen, including clients that
  exit on their own.
- `ignore_class` / `ignore_title` opt windows out entirely (no bar, no
  reserved space). Re-evaluated live on title/class changes and config
  reload.
- The bar and text ignore the renderer's opacity multiplier: only the
  configured colours' alpha, scaled by `opacity.active` / `opacity.inactive`,
  controls them.
- **With 3LA-Corners**, the top brackets frame the outside of the bar.
  **With 3LA-MilSpec**, the bar is stacked outside the frame and the frame
  grows to enclose it — but that gives two title bars if MilSpec's own is on.
