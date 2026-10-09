# Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
"""Consistency check for the Phase 1 spec assets (CTest fast tier).

Owns: the invariants that keep tokens, fonts, icons and reference images in step:
  - tokens.json has identical colour name sets for the dark and light themes, all valid hex colours;
  - every image listed in tests/reference/openpencil/manifest.json exists and is a readable PNG of the
    recorded clip size (full screens equal the recorded viewport);
  - every icon in assets/icons/manifest.json has an SVG, and every icon rendered for reference exists at all sizes;
  - fonts referenced by tokens.json exist and metrics.json covers each face.
Exit code 0 = pass; every violation is printed.
"""
import json
import pathlib
import re
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import png  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
HEX = re.compile(r"^#[0-9a-f]{6}([0-9a-f]{2})?$")


def main():
    problems = []
    tokens = json.loads((ROOT / "assets/theme/tokens.json").read_text(encoding="utf-8"))
    dark, light = tokens["themes"]["dark"], tokens["themes"]["light"]
    if set(dark) != set(light):
        problems.append(f"theme key mismatch: {sorted(set(dark) ^ set(light))}")
    for name in ("danger", "primary", "border-strong", "panel-secondary"):
        for theme in ("dark", "light"):
            if name not in tokens["themes"][theme]:
                problems.append(f"missing widget colour {theme}.{name}")
    for theme, colors in tokens["themes"].items():
        for name, value in colors.items():
            if not HEX.match(value):
                problems.append(f"bad colour {theme}.{name} = {value}")

    for weight, fname in tokens["font"]["files"].items():
        if not (ROOT / "assets/fonts" / fname).is_file():
            problems.append(f"missing font file {fname}")
    metrics = json.loads((ROOT / "assets/fonts/metrics.json").read_text(encoding="utf-8"))
    have = {f["file"] for f in metrics["faces"]}
    for fname in tokens["font"]["files"].values():
        if fname not in have:
            problems.append(f"no metrics for {fname}")

    ref_dir = ROOT / "tests/reference/openpencil"
    manifest = json.loads((ref_dir / "manifest.json").read_text(encoding="utf-8"))
    vw, vh = manifest["viewport"]["width"], manifest["viewport"]["height"]
    for entry in manifest["images"]:
        path = ref_dir / entry["file"]
        if not path.is_file():
            problems.append(f"missing reference image {entry['file']}")
            continue
        head = path.read_bytes()[:33]  # signature + IHDR chunk: enough to read the size cheaply
        if head[:8] != png.SIGNATURE or head[12:16] != b"IHDR":
            problems.append(f"{entry['file']}: not a PNG")
            continue
        w, h = struct.unpack(">II", head[16:24])
        if entry["clip"] is None and (w, h) != (vw, vh):
            problems.append(f"{entry['file']}: expected {vw}x{vh}, got {w}x{h}")
        # Chrome rounds fractional clip rectangles to whole pixels, so allow one pixel of slack.
        if entry["clip"] and (abs(w - entry["clip"]["width"]) > 1.01 or abs(h - entry["clip"]["height"]) > 1.01):
            problems.append(f"{entry['file']}: size {w}x{h} differs from clip")

    icons = json.loads((ROOT / "assets/icons/manifest.json").read_text(encoding="utf-8"))
    for name in icons["icons"]:
        if not (ROOT / "assets/icons/lucide" / f"{name}.svg").is_file():
            problems.append(f"missing icon svg {name}")
    rendered = json.loads((ROOT / "tests/reference/icons/manifest.json").read_text(encoding="utf-8"))
    for size in rendered["sizes"]:
        for name in rendered["icons"]:
            if not (ROOT / "tests/reference/icons" / str(size) / f"{name}.png").is_file():
                problems.append(f"missing rendered icon {size}/{name}")
    unextracted = set(icons["usedButNotExtracted"])
    if unextracted - {"rotate-ccw"}:
        problems.append(f"icons used by the UI but not extracted: {sorted(unextracted - {'rotate-ccw'})}")

    for p in problems:
        print(p)
    print(f"assets_check: {'FAIL' if problems else 'ok'} ({len(problems)} problems; "
          f"{len(dark)} colours/theme, {len(manifest['images'])} reference images, {len(icons['icons'])} icons)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
