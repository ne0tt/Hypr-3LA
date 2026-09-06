#!/usr/bin/env python3
"""Generate the viewer's data file from the plugin sources.

The viewer must never carry its own copy of the shader: a copy drifts, and a
tuner that shows something other than what the compositor draws is worse than
no tuner. So the GLSL, the config option list, their defaults and their ranges
are all extracted from 3LA-GlitchClose/ at build time.

Re-run (or `make`) after touching shader.hpp or main.cpp.
"""

import base64
import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
PLUGIN = HERE.parent / "3LA-GlitchClose"

# uniform names that cannot be derived from the config key by the rule below
# ("noise" would collide with the vnoise() helper in the shader)
UNIFORM_ALIAS = {"noise": "noiseAmount"}


def uniform_name(key: str) -> str:
    """config key -> GLSL uniform: col.backdrop -> colBackdrop,
    backdrop_alpha -> backdropAlpha."""
    if key in UNIFORM_ALIAS:
        return UNIFORM_ALIAS[key]
    head, *rest = re.split(r'[._]', key)
    return head + "".join(w[:1].upper() + w[1:] for w in rest)

# knobs the viewer drives from the timeline rather than a slider
TIMELINE_KEYS = {"duration", "fade", "close_at"}

# config keys with no shader uniform (close plumbing and window filtering, not
# visuals). `hold` is compositor-side timing: it waits on the real window going
# away, which the viewer has no equivalent of.
NON_VISUAL = {"hold", "min_size", "ignore_children", "ignore_class", "ignore_title"}

# the caption is composited by the plugin as a rect + text texture, not by the
# shader, so these have no uniform and the viewer draws them as an HTML overlay
CAPTION = {"text", "font", "text_size", "text_alpha", "text_blink", "text_at",
           "text_padding", "text_bg_round", "text_bg_alpha", "col.text", "col.text_bg"}

# sensible slider ceilings for options the plugin leaves unbounded
MAX_HINT = {"duration": 5000, "fade": 2000, "text_size": 96, "text_blink": 1000,
            "text_padding": 80, "text_bg_round": 40}


def raw_string(src: str, name: str) -> str:
    """Pull the body out of `inline const std::string NAME = R"#(...)#";`"""
    m = re.search(r'\b%s\s*=\s*R"#\((.*?)\)#";' % re.escape(name), src, re.S)
    if not m:
        sys.exit(f"sync.py: could not find raw string {name} in shader.hpp")
    return m.group(1)


CONFIG_RE = re.compile(
    r'makeShared<Config::Values::C(?P<type>Int|Float|Color|String)Value>\(\s*'
    r'"plugin:3la_glitch_close:(?P<key>[\w.]+)"\s*,\s*'
    r'"(?P<desc>(?:[^"\\]|\\.)*)"\s*,\s*'
    r'(?P<default>"(?:[^"\\]|\\.)*"|[^,)]+?)\s*'
    r'(?:,\s*Config::Values::S\w+ValueOptions\{(?P<opts>[^}]*)\})?\s*\)',
    re.S,
)


def opt(opts: str, field: str):
    if not opts:
        return None
    m = re.search(r'\.%s\s*=\s*([-\d.]+)F?' % field, opts)
    return float(m.group(1)) if m else None


def parse_config(src: str):
    out = []
    for m in CONFIG_RE.finditer(src):
        typ, key = m.group("type"), m.group("key")
        raw_default = m.group("default").strip()
        if typ == "String" and key not in CAPTION:
            continue  # regex filters, nothing to tune visually
        if typ == "Color":
            default = None  # 0 = "use the built-in default"
        elif typ == "String":
            default = raw_default.strip('"')
        else:
            default = float(raw_default.rstrip("Ff"))
        out.append({
            "key": key,
            "type": typ.lower(),
            "desc": re.sub(r'\s+', ' ', m.group("desc")),
            "default": default,
            "min": opt(m.group("opts"), "min"),
            "max": opt(m.group("opts"), "max") or MAX_HINT.get(key),
            "uniform": uniform_name(key),
            "role": ("timeline" if key in TIMELINE_KEYS
                     else "skip" if key in NON_VISUAL
                     else "caption" if key in CAPTION
                     else "color" if typ == "Color"
                     else "slider"),
        })
    return out


def main() -> None:
    shader_src = (PLUGIN / "shader.hpp").read_text(encoding="utf-8")
    main_src = (PLUGIN / "main.cpp").read_text(encoding="utf-8")

    vert = raw_string(shader_src, "GLITCH_VERT")
    frag = raw_string(shader_src, "GLITCH_FRAG")
    config = parse_config(main_src)

    sliders = [c for c in config if c["role"] == "slider"]
    if len(sliders) < 8:
        sys.exit(f"sync.py: only parsed {len(sliders)} sliders from main.cpp -- "
                 "the config registration format probably changed")

    # every control must correspond to a real uniform, or the viewer would
    # silently show a slider that does nothing
    for c in config:
        if c["role"] not in ("slider", "color"):
            continue  # caption controls are composited, not shader uniforms
        glsl_type = "vec3" if c["role"] == "color" else "float"
        if not re.search(r'uniform\s+%s\s+%s\s*[;/]' % (glsl_type, re.escape(c["uniform"])), frag):
            sys.exit(f"sync.py: config key '{c['key']}' has no matching "
                     f"'uniform {glsl_type} {c['uniform']}' in the fragment shader")

    bg = HERE / "background-src.jpg"
    bg_uri = ""
    if bg.exists():
        bg_uri = "data:image/jpeg;base64," + base64.b64encode(bg.read_bytes()).decode()

    payload = {
        "GLITCH_VERT": vert,
        "GLITCH_FRAG": frag,
        "CONFIG": config,
        "DEFAULT_BG": bg_uri,
    }

    # plain globals, not an ES module: file:// blocks module imports, and this
    # page is meant to open by double-clicking with no web server
    body = "// GENERATED by sync.py -- do not edit. Run `make` in this directory.\n"
    for k, v in payload.items():
        body += f"window.{k} = {json.dumps(v)};\n"
    (HERE / "generated.js").write_text(body, encoding="utf-8")

    # side-car copies so `make check` can run them through glslangValidator
    (HERE / ".build").mkdir(exist_ok=True)
    (HERE / ".build/glitch.vert").write_text(vert, encoding="utf-8")
    (HERE / ".build/glitch.frag").write_text(frag, encoding="utf-8")

    print(f"generated.js: {len(sliders)} sliders, "
          f"{len([c for c in config if c['role'] == 'color'])} colors, "
          f"{len([c for c in config if c['role'] == 'caption'])} caption controls, "
          f"background {len(bg_uri)//1024} KiB")


if __name__ == "__main__":
    main()
