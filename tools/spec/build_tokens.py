# Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
"""Builds assets/theme/tokens.json from the OpenPencil reference checkout.

Owns: the one neutral design-token file the toolkit's theme loader (slice 3.10) reads.
Inputs (read-only, reference material): reference/OpenPencil/src/app.css (dark and light theme
variables), the compiled Tailwind CSS in dist/assets (spacing, radius, type scale, shadows,
Tailwind palette in oklch), and docs/spec/measurements.json (computed widget metrics).
Tokens and visual values only are reused (OpenPencil is MIT, Copyright (c) 2026 Danila Poyarkov;
notice kept in docs/third-party.md). No source code is copied.
Usage: python tools/spec/build_tokens.py
"""
import glob
import json
import math
import os
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
# The reference checkout is not versioned, so a secondary worktree points at the main one with R1UI_REFERENCE_ROOT.
OP = pathlib.Path(os.environ.get("R1UI_REFERENCE_ROOT", ROOT / "reference")) / "OpenPencil"
OUT = ROOT / "assets" / "theme" / "tokens.json"
COMMIT = "dd3161e44"


def rgb_to_hex(r, g, b, a=1.0):
    h = "#%02x%02x%02x" % (round(r), round(g), round(b))
    return h + ("%02x" % round(a * 255) if a < 0.999 else "")


def normalize_color(v):
    v = v.strip()
    m = re.fullmatch(r"rgb\(\s*(\d+)[ ,]+(\d+)[ ,]+(\d+)(?:\s*/\s*([\d.]+))?\s*\)", v)
    if m:
        return rgb_to_hex(*map(int, m.groups()[:3]), float(m.group(4) or 1))
    m = re.fullmatch(r"#([0-9a-fA-F]{3})", v)
    if m:
        return "#" + "".join(c * 2 for c in m.group(1)).lower()
    m = re.fullmatch(r"#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})", v)
    if m:
        return v.lower()
    raise ValueError("unsupported colour: " + v)


def mix_hex(hex_color, target, amount):
    """Moves each channel of #rrggbb towards `target` (0 = black, 255 = white) by `amount` of the way."""
    channels = [int(hex_color[i:i + 2], 16) for i in (1, 3, 5)]
    return rgb_to_hex(*[c + (target - c) * amount for c in channels])


def add_widget_colors(colors):
    """Colours OpenPencil's widget code names but its CSS never defines (danger, primary, border-strong).
    danger = the error token, primary = accent, border-strong = border moved 8% towards white (dark) or
    black (light). panel-secondary is kept even though OpenPencil's light build drops it."""
    for theme, target in (("dark", 255), ("light", 0)):
        c = colors[theme]
        c["danger"] = c["error"]
        c["primary"] = c["accent"]
        c["border-strong"] = mix_hex(c["border"], target, 0.08)


def oklch_to_hex(l, c, h):
    """Converts CSS oklch (l in 0..1) to sRGB hex, clamping out-of-gamut channels."""
    hr = math.radians(h)
    a, b = c * math.cos(hr), c * math.sin(hr)
    l_ = (l + 0.3963377774 * a + 0.2158037573 * b) ** 3
    m_ = (l - 0.1055613458 * a - 0.0638541728 * b) ** 3
    s_ = (l - 0.0894841775 * a - 1.2914855480 * b) ** 3
    rgb = [4.0767416621 * l_ - 3.3077115913 * m_ + 0.2309699292 * s_,
           -1.2684380046 * l_ + 2.6097574011 * m_ - 0.3413193965 * s_,
           -0.0041960863 * l_ - 0.7034186147 * m_ + 1.7076147010 * s_]

    def gamma(x):
        x = min(max(x, 0.0), 1.0)
        return 12.92 * x if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055

    return rgb_to_hex(*[gamma(x) * 255 for x in rgb])


def css_vars(block):
    return dict(re.findall(r"--([\w-]+)\s*:\s*([^;]+);", block))


def block_after(text, start_pat):
    i = re.search(start_pat, text).end()
    depth, j = 1, i
    while depth:
        depth += {"{": 1, "}": -1}.get(text[j], 0)
        j += 1
    return text[i:j - 1]


def main():
    app_css = (OP / "src" / "app.css").read_text(encoding="utf-8")
    dark = css_vars(block_after(app_css, r"@theme\s*\{"))
    light = css_vars(block_after(app_css, r"html\[data-theme='light'\]\s*\{"))
    compiled = pathlib.Path(glob.glob(str(OP / "dist" / "assets" / "index-*.css"))[0]).read_text(encoding="utf-8")
    root = css_vars(re.search(r":root,\s*:host\s*\{(.*?)\}", compiled, re.S).group(1))

    colors = {t: {k[len("color-"):]: normalize_color(v) for k, v in vs.items() if k.startswith("color-")}
              for t, vs in (("dark", dark), ("light", light))}

    add_widget_colors(colors)

    palette = {}
    for k, v in root.items():
        m = re.fullmatch(r"oklch\(([\d.]+)%\s+([\d.]+)\s+([\d.]+)\)", v.strip())
        if k.startswith("color-") and m:
            palette[k[len("color-"):]] = oklch_to_hex(float(m.group(1)) / 100, float(m.group(2)), float(m.group(3)))
    palette["black"], palette["white"] = "#000000", "#ffffff"

    meas = json.loads((ROOT / "docs" / "spec" / "measurements.json").read_text(encoding="utf-8"))

    def rem(name):
        return float(root[name].replace("rem", "")) * 16

    tokens = {
        "meta": {
            "name": "R1GUI design tokens",
            "source": "OpenPencil (MIT, Copyright (c) 2026 Danila Poyarkov)",
            "sourceCommit": COMMIT,
            "generatedBy": "tools/spec/build_tokens.py",
            "units": "logical pixels at 100% scale unless stated; colours are sRGB #rrggbb or #rrggbbaa",
            "note": "danger, primary and border-strong are derived (build_tokens.py add_widget_colors): OpenPencil code names them but its CSS never defines them",
        },
        "themes": colors,
        "palette": palette,
        "space": {
            "unit": rem("spacing"),
            "control": 26, "panel-x": 12, "panel-y": 8, "panel": 6, "panel-rail": 26, "panel-icon": 14,
        },
        "radius": {
            "panel": 4, "sm": rem("radius-sm"), "md": rem("radius-md"), "lg": rem("radius-lg"),
            "xl": rem("radius-xl"), "3xl": rem("radius-3xl"), "default": 4, "full": 9999,
        },
        "font": {
            "family": "Inter",
            "fallback": ["system-ui", "sans-serif"],
            "files": {"400": "Inter-Regular.ttf", "500": "Inter-Medium.ttf", "600": "Inter-SemiBold.ttf",
                      "700": "Inter-Bold.ttf", "800": "Inter-ExtraBold.ttf"},
            "bodySize": 13,
            "size": {"9": 9, "10": 10, "11": 11, "xs": rem("text-xs"), "13": 13, "sm": rem("text-sm"),
                     "base": rem("text-base"), "lg": rem("text-lg"), "2xl": rem("text-2xl")},
            "lineHeight": {"xs": 16, "sm": 20, "base": 24, "lg": 28, "2xl": 32, "tight": 1.25, "snug": 1.375,
                           "relaxed": 1.625},
            "weight": {"normal": 400, "medium": 500, "semibold": 600, "bold": 700},
            "tracking": {"wide": 0.025, "wider": 0.05},
        },
        "shadow": {
            "sm": [[0, 1, 3, 0, "#0000001a"], [0, 1, 2, -1, "#0000001a"]],
            "md": [[0, 4, 6, -1, "#0000001a"], [0, 2, 4, -2, "#0000001a"]],
            "lg": [[0, 10, 15, -3, "#0000001a"], [0, 4, 6, -4, "#0000001a"]],
            "xl": [[0, 20, 25, -5, "#0000001a"], [0, 8, 10, -6, "#0000001a"]],
            "2xl": [[0, 25, 50, -12, "#00000040"]],
            "overlay": [[0, 8, 30, 0, "#00000066"]],
            "_format": "[offsetX, offsetY, blur, spread, colour]; blur is the CSS blur radius",
        },
        "motion": {"duration": 0.15, "easing": [0.4, 0, 0.2, 1], "blurXl": 24},
        "widget": {
            "_source": "sizes from OpenPencil src/theme/*.ts and src/components/ui/*.ts, verified against docs/spec/measurements.json",
            "button": {"sm": {"height": 28, "padX": 8, "text": "xs"}, "md": {"height": 32, "padX": 12, "text": "xs"},
                       "icon": 32, "iconSm": 28},
            "iconButton": {"sm": {"size": 20, "radius": 4, "text": "sm"}, "md": {"size": 26, "radius": 4}},
            "field": {"height": 26, "radius": 4, "borderWidth": 1, "padX": 6},
            "segmented": {"itemHeight": 22, "padding": 2, "gap": 2, "radius": 2},
            "switch": {"sm": {"w": 28, "h": 16, "thumb": 12}, "md": {"w": 36, "h": 20, "thumb": 16}},
            "tabBar": {"height": 36, "triggerPadX": 12, "maxTabWidth": 192},
            "toolbar": {"button": 32, "icon": 16, "flyoutTriggerWidth": 12, "radius": 8},
            "menu": {"padding": 4, "itemPadX": 8, "itemPadY": 6, "radius": 8, "separator": 1, "itemRadius": 6},
            "tooltip": {"padX": 8, "padY": 4, "radius": 6},
            "popover": {"radius": 8},
            "dialog": {"radius": 12},
            "layerRow": {"padY": 4, "gap": 4, "icon": 12, "disclosure": 16, "radius": 4},
            "panelSection": {"headerHeight": 26},
            "colorSlider": {"trackHeight": 12, "thumb": 14},
            "checkerboard": {"cell": 8},
            "scrollbarThin": {"size": 6, "radius": 3},
        },
        "measured": {k: {"rect": [round(v["rect"][a], 2) for a in ("width", "height")],
                         "fontSize": v["style"]["fontSize"], "fontWeight": v["style"]["fontWeight"],
                         "radius": v["style"]["borderTopLeftRadius"]}
                     for k, v in meas["widgets"].items() if k.startswith("dark/") and not v.get("missing")},
    }
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(tokens, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}: dark {len(colors['dark'])} light {len(colors['light'])} palette {len(palette)}")


if __name__ == "__main__":
    main()
