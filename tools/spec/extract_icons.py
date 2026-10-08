# Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
"""Extracts the Lucide icons the OpenPencil UI uses from its built bundle into standalone SVG files.

Owns: assets/icons/lucide/*.svg and assets/icons/manifest.json (slice 1.4).
Why: the checkout has no node_modules, but each icon ships as a small chunk in dist/assets whose body is the
SVG geometry; reading the built chunks avoids downloading anything. Lucide is ISC licensed (assets/icons/lucide/LICENSE).
Only SVG element names and attributes from an allowlist are written, so a hostile chunk cannot inject markup.
Usage: python tools/spec/extract_icons.py
"""
import glob
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
DIST = ROOT / "reference" / "OpenPencil" / "dist" / "assets"
OUT = ROOT / "assets" / "icons" / "lucide"
TAGS = {"path", "circle", "rect", "line", "polyline", "polygon", "ellipse"}
ATTRS = {"d", "cx", "cy", "r", "rx", "ry", "x", "y", "x1", "y1", "x2", "y2", "width", "height", "points",
         "transform", "opacity"}
NAME = re.compile(r"name:`lucide-([a-z0-9-]+)`")
HTML_ELEMENT = re.compile(r"<(path|circle|rect|line|polyline|polygon|ellipse)\s([^<>]*?)\s*/?>")
HTML_ATTR = re.compile(r'([\w-]+)="([^"]*)"')
ELEMENT = re.compile(r"[\w$]+\(`(\w+)`,\{([^{}]*)\}")
ATTR = re.compile(r'(?:"([\w-]+)"|([\w-]+)):`([^`]*)`')
VALUE_OK = re.compile(r"^[0-9A-Za-z .,\-+()eE]*$")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    icons = {}
    for f in sorted(glob.glob(str(DIST / "*.js"))):
        text = pathlib.Path(f).read_text(encoding="utf-8", errors="replace")
        for m in NAME.finditer(text):
            # The render function that builds the SVG sits immediately before the component definition.
            start = text.rfind("function", max(0, m.start() - 4000), m.start())
            span = text[start if start >= 0 else max(0, m.start() - 1500):m.start()]
            parts = []
            for tag, body in ELEMENT.findall(span):
                if tag not in TAGS:
                    continue
                attrs = []
                for q, bare, val in ATTR.findall(body):
                    key = q or bare
                    if key in ATTRS and VALUE_OK.match(val):
                        attrs.append(f'{key}="{val}"')
                parts.append(f"<{tag} " + " ".join(attrs) + "/>")
            for tag, body in HTML_ELEMENT.findall(span):
                attrs = [f'{k}="{v}"' for k, v in HTML_ATTR.findall(body)
                         if (k in ATTRS or (k == "fill" and v == "currentColor")) and VALUE_OK.match(v)]
                parts.append(f"<{tag} " + " ".join(attrs) + "/>")
            if parts and m.group(1) not in icons:
                icons[m.group(1)] = parts
    for name, parts in sorted(icons.items()):
        svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" '
               'stroke-width="2" stroke-linecap="round" stroke-linejoin="round">' + "".join(parts) + "</svg>\n")
        (OUT / f"{name}.svg").write_text(svg, encoding="utf-8")
    used = set(re.findall(r"~icons/lucide/([a-z0-9-]+)", "\n".join(
        p.read_text(encoding="utf-8", errors="replace") for p in (ROOT / "reference" / "OpenPencil" / "src").rglob("*")
        if p.suffix in {".vue", ".ts"})))
    manifest = {"set": "Lucide", "license": "ISC (assets/icons/lucide/LICENSE)", "viewBox": 24, "strokeWidth": 2,
                "defaultSizeEm": 1.2, "count": len(icons), "icons": sorted(icons),
                "usedBySource": sorted(used), "usedButNotExtracted": sorted(used - set(icons))}
    (ROOT / "assets" / "icons" / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")
    print(f"extracted {len(icons)} icons; source references {len(used)}; missing {manifest['usedButNotExtracted']}")


if __name__ == "__main__":
    main()
