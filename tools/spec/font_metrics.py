# Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
"""Extracts vertical metrics and ASCII advance widths from the Inter TrueType files.

Owns: assets/fonts/metrics.json, the oracle the text slices (3.4, 3.5) compare FreeType/HarfBuzz output
against, and the source of the line-height numbers in the design tokens.
Reads only the standard sfnt tables head, hhea, OS/2, maxp, cmap (format 4 and 12) and hmtx with the
standard library; every offset and length is bounds-checked because font files are external input.
Usage: python tools/spec/font_metrics.py
"""
import json
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parents[2]
FONTS = ROOT / "assets" / "fonts"


class FontError(ValueError):
    pass


def slice_checked(data, off, length):
    if off < 0 or length < 0 or off + length > len(data):
        raise FontError("table outside file")
    return data[off:off + length]


def read_tables(data):
    num = struct.unpack(">H", slice_checked(data, 4, 2))[0]
    tables = {}
    for i in range(num):
        tag, _, off, length = struct.unpack(">4sIII", slice_checked(data, 12 + 16 * i, 16))
        slice_checked(data, off, length)
        tables[tag.decode("latin-1")] = (off, length)
    return tables


def cmap_lookup(data, tables):
    off, length = tables["cmap"]
    table = slice_checked(data, off, length)
    _, count = struct.unpack(">HH", table[:4])
    best = None
    for i in range(count):
        pid, eid, sub = struct.unpack(">HHI", table[4 + 8 * i:12 + 8 * i])
        if (pid, eid) in ((3, 10), (3, 1), (0, 4), (0, 3)):
            best = sub if best is None or (pid, eid) == (3, 10) else best
    if best is None:
        raise FontError("no Unicode cmap")
    fmt = struct.unpack(">H", table[best:best + 2])[0]
    mapping = {}
    if fmt == 4:
        segx2 = struct.unpack(">H", table[best + 6:best + 8])[0]
        seg = segx2 // 2
        ends = struct.unpack(f">{seg}H", table[best + 14:best + 14 + segx2])
        starts = struct.unpack(f">{seg}H", table[best + 16 + segx2:best + 16 + 2 * segx2])
        deltas = struct.unpack(f">{seg}h", table[best + 16 + 2 * segx2:best + 16 + 3 * segx2])
        ro_base = best + 16 + 3 * segx2
        offsets = struct.unpack(f">{seg}H", table[ro_base:ro_base + segx2])
        for s in range(seg):
            for code in range(starts[s], ends[s] + 1):
                if code == 0xFFFF:
                    continue
                if offsets[s] == 0:
                    gid = (code + deltas[s]) & 0xFFFF
                else:
                    pos = ro_base + 2 * s + offsets[s] + 2 * (code - starts[s])
                    gid = struct.unpack(">H", table[pos:pos + 2])[0]
                    gid = (gid + deltas[s]) & 0xFFFF if gid else 0
                if gid:
                    mapping[code] = gid
    elif fmt == 12:
        groups = struct.unpack(">I", table[best + 12:best + 16])[0]
        for g in range(groups):
            lo, hi, start = struct.unpack(">III", table[best + 16 + 12 * g:best + 28 + 12 * g])
            for code in range(lo, min(hi, 0x2FFFF) + 1):
                mapping[code] = start + code - lo
    else:
        raise FontError(f"unsupported cmap format {fmt}")
    return mapping


def metrics_for(path):
    data = path.read_bytes()
    tables = read_tables(data)
    head = slice_checked(data, *tables["head"])
    upem = struct.unpack(">H", head[18:20])[0]
    hhea = slice_checked(data, *tables["hhea"])
    asc, desc, gap = struct.unpack(">hhh", hhea[4:10])
    n_hmetrics = struct.unpack(">H", hhea[34:36])[0]
    os2 = slice_checked(data, *tables["OS/2"])
    version = struct.unpack(">H", os2[:2])[0]
    weight = struct.unpack(">H", os2[4:6])[0]
    sel = struct.unpack(">H", os2[62:64])[0]
    typo_asc, typo_desc, typo_gap, win_asc, win_desc = struct.unpack(">hhhHH", os2[68:78])
    x_height = cap_height = None
    if version >= 2 and len(os2) >= 90:
        x_height, cap_height = struct.unpack(">hh", os2[86:90])
    hmtx = slice_checked(data, *tables["hmtx"])
    cmap = cmap_lookup(data, tables)

    def advance(gid):
        gid = min(gid, n_hmetrics - 1)
        return struct.unpack(">H", hmtx[4 * gid:4 * gid + 2])[0]

    return {
        "file": path.name, "weight": weight, "unitsPerEm": upem,
        "hhea": {"ascender": asc, "descender": desc, "lineGap": gap},
        "typo": {"ascender": typo_asc, "descender": typo_desc, "lineGap": typo_gap,
                 "useTypoMetrics": bool(sel & (1 << 7))},
        "win": {"ascent": win_asc, "descent": win_desc},
        "xHeight": x_height, "capHeight": cap_height,
        "glyphCount": len(cmap),
        "asciiAdvance": {chr(c): advance(cmap[c]) for c in range(32, 127) if c in cmap},
    }


def main():
    out = {"font": "Inter 4.001", "license": "SIL Open Font License 1.1 (assets/fonts/OFL.txt)",
           "note": "advances are in font units (divide by unitsPerEm, multiply by pixel size); unkerned",
           "faces": [metrics_for(p) for p in sorted(FONTS.glob("Inter-*.ttf"))]}
    (FONTS / "metrics.json").write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")
    f = out["faces"][0]
    print(f"wrote assets/fonts/metrics.json: {len(out['faces'])} faces; {f['file']} upem {f['unitsPerEm']} "
          f"hhea {f['hhea']} xHeight {f['xHeight']} capHeight {f['capHeight']}")


if __name__ == "__main__":
    main()
