# Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
"""Pixel comparison of a candidate screenshot against a reference screenshot.

Owns: the visual-comparison method used by widget tests from Phase 3 on (slice 1.6).
Method: images must have identical size. A pixel FAILS when any of R, G, B differs by more than
`channelTolerance` (0..255). The comparison PASSES when the fraction of failing pixels is at most
`maxFailingFraction`. It also reports max and mean absolute channel difference and the bounding box of
failing pixels, and can write a diff image (failing pixels red over a dimmed candidate).
Tolerances come from tests/reference/tolerance.json; named profiles allow looser limits for text-heavy
regions. Profile values are PROVISIONAL until calibrated against real toolkit output in Phase 4.
Usage:
  python tools/spec/imgdiff.py <reference.png> <candidate.png> [--profile name] [--diff out.png]
  python tools/spec/imgdiff.py --selftest
Exit code 0 = pass, 1 = fail, 2 = usage or input error.
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import png  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOLERANCE_FILE = ROOT / "tests" / "reference" / "tolerance.json"


def load_profile(name):
    data = json.loads(TOLERANCE_FILE.read_text(encoding="utf-8"))
    profile = dict(data["default"])
    if name != "default":
        profile.update(data["profiles"][name])
    return profile


def compare(ref, cand, channel_tolerance, max_failing_fraction):
    (rw, rh, rpix), (cw, ch, cpix) = ref, cand
    if (rw, rh) != (cw, ch):
        raise ValueError(f"size mismatch: reference {rw}x{rh}, candidate {cw}x{ch}")
    failing = 0
    max_diff = 0
    total = 0
    minx, miny, maxx, maxy = rw, rh, -1, -1
    mask = bytearray(rw * rh)
    for i in range(rw * rh):
        o = i * 4
        d = max(abs(rpix[o] - cpix[o]), abs(rpix[o + 1] - cpix[o + 1]), abs(rpix[o + 2] - cpix[o + 2]))
        if d:
            total += d
            if d > max_diff:
                max_diff = d
            if d > channel_tolerance:
                failing += 1
                mask[i] = 1
                x, y = i % rw, i // rw
                minx, miny, maxx, maxy = min(minx, x), min(miny, y), max(maxx, x), max(maxy, y)
    fraction = failing / (rw * rh)
    return {
        "width": rw, "height": rh, "failingPixels": failing, "failingFraction": fraction,
        "maxChannelDiff": max_diff, "meanDiff": total / (rw * rh),
        "failingBounds": None if failing == 0 else [minx, miny, maxx, maxy],
        "pass": fraction <= max_failing_fraction,
    }, mask


def write_diff(path, cand, mask):
    w, h, pix = cand
    out = bytearray(pix)
    for i in range(w * h):
        o = i * 4
        if mask[i]:
            out[o], out[o + 1], out[o + 2], out[o + 3] = 255, 0, 0, 255
        else:
            out[o], out[o + 1], out[o + 2] = pix[o] // 3, pix[o + 1] // 3, pix[o + 2] // 3
    pathlib.Path(path).write_bytes(png.encode(w, h, bytes(out)))


def selftest():
    """Oracle: identical passes; a perturbed block fails; size mismatch is rejected."""
    w, h = 40, 30
    base = bytes([30, 60, 90, 255]) * (w * h)
    ref = (w, h, base)
    ok, _ = compare(ref, (w, h, base), 8, 0.0)
    assert ok["pass"] and ok["failingPixels"] == 0, "identical images must pass"
    bumped = bytearray(base)
    for y in range(5, 15):
        for x in range(5, 15):
            bumped[(y * w + x) * 4] = 200
    bad, _ = compare(ref, (w, h, bytes(bumped)), 8, 0.01)
    assert not bad["pass"] and bad["failingPixels"] == 100 and bad["failingBounds"] == [5, 5, 14, 14], bad
    within = bytearray(base)
    within[0] += 5
    near, _ = compare(ref, (w, h, bytes(within)), 8, 0.0)
    assert near["pass"] and near["maxChannelDiff"] == 5, "difference within tolerance must pass"
    try:
        compare(ref, (w + 1, h, bytes([0, 0, 0, 255]) * ((w + 1) * h)), 8, 0.0)
    except ValueError:
        pass
    else:
        raise AssertionError("size mismatch must raise")
    roundtrip = png.decode(png.encode(w, h, bytes(bumped)))
    assert roundtrip == (w, h, bytes(bumped)), "png encode/decode round trip"
    print("imgdiff selftest: ok")
    return 0


def main(argv):
    if "--selftest" in argv:
        return selftest()
    if len(argv) < 2:
        print(__doc__)
        return 2
    profile = load_profile(argv[argv.index("--profile") + 1] if "--profile" in argv else "default")
    try:
        ref = png.decode(pathlib.Path(argv[0]).read_bytes())
        cand = png.decode(pathlib.Path(argv[1]).read_bytes())
        result, mask = compare(ref, cand, profile["channelTolerance"], profile["maxFailingFraction"])
    except (ValueError, OSError) as e:
        print(f"imgdiff: {e}")
        return 2
    if "--diff" in argv:
        write_diff(argv[argv.index("--diff") + 1], cand, mask)
    print(json.dumps(result))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
