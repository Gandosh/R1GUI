# Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
"""Minimal PNG reader/writer on the standard library only.

Owns: decoding 8-bit non-interlaced RGB/RGBA/grey PNGs to RGBA bytes and writing RGBA PNGs.
Why: reference screenshots and toolkit screenshots are compared in the pixel-diff tool without
third-party imaging packages. Inputs are validated (signature, chunk lengths, sizes, filters) because
files may come from other tools; anything unsupported raises ValueError.
Callers: tools/spec/imgdiff.py, tools/spec/png_to_raw.py.
"""
import struct
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"
MAX_PIXELS = 64 * 1024 * 1024


def decode(data: bytes):
    """Returns (width, height, rgba_bytes)."""
    if data[:8] != SIGNATURE:
        raise ValueError("not a PNG")
    pos, ihdr, idat = 8, None, []
    while pos + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if len(body) != length:
            raise ValueError("truncated chunk")
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
        pos += 12 + length
    if ihdr is None:
        raise ValueError("missing IHDR")
    width, height, depth, ctype, _, _, interlace = ihdr
    if depth != 8 or interlace != 0 or ctype not in (0, 2, 6):
        raise ValueError(f"unsupported PNG (depth {depth}, colour type {ctype}, interlace {interlace})")
    if width == 0 or height == 0 or width * height > MAX_PIXELS:
        raise ValueError("unreasonable PNG size")
    bpp = {0: 1, 2: 3, 6: 4}[ctype]
    stride = width * bpp
    raw = zlib.decompress(b"".join(idat))
    if len(raw) != (stride + 1) * height:
        raise ValueError("PNG data size mismatch")
    out = bytearray(stride * height)
    prev = bytearray(stride)
    for y in range(height):
        ft = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if ft == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif ft == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif ft == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 255
        elif ft != 0:
            raise ValueError("bad PNG filter")
        out[y * stride:(y + 1) * stride] = line
        prev = line
    if ctype == 6:
        return width, height, bytes(out)
    rgba = bytearray(width * height * 4)
    if ctype == 2:
        rgba[0::4], rgba[1::4], rgba[2::4], rgba[3::4] = out[0::3], out[1::3], out[2::3], b"\xff" * (width * height)
    else:
        rgba[0::4] = rgba[1::4] = rgba[2::4] = out
        rgba[3::4] = b"\xff" * (width * height)
    return width, height, bytes(rgba)


def encode(width: int, height: int, rgba: bytes) -> bytes:
    if len(rgba) != width * height * 4:
        raise ValueError("pixel buffer size mismatch")
    raw = b"".join(b"\x00" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    return (SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
