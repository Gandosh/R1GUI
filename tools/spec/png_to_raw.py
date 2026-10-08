# Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
"""Converts reference PNGs to the raw image format the preview loads without a PNG decoder.

Owns: the R1IMG container: 8-byte magic "R1IMG001", u32 little-endian width, u32 height, then
width*height*4 bytes of BGRA, 8-bit, sRGB, premultiplication none (matches B8G8R8A8_UNORM swapchains).
Usage: python tools/spec/png_to_raw.py <png-dir> <out-dir> [--only <substring>]
Output files keep the relative path with the extension changed to .r1img. Generated files go under
build/ and are never committed.
"""
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import png  # noqa: E402


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    src, dst = pathlib.Path(argv[0]), pathlib.Path(argv[1])
    only = argv[argv.index("--only") + 1] if "--only" in argv else ""
    count = 0
    for f in sorted(src.rglob("*.png")):
        if only not in f.name:
            continue
        w, h, rgba = png.decode(f.read_bytes())
        bgra = bytearray(rgba)
        bgra[0::4], bgra[2::4] = rgba[2::4], rgba[0::4]
        out = dst / f.relative_to(src).with_suffix(".r1img")
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(b"R1IMG001" + struct.pack("<II", w, h) + bytes(bgra))
        count += 1
    print(f"png_to_raw: wrote {count} files to {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
