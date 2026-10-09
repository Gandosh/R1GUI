# Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
"""Tolerance profile calibration report from the visual tests' own output.

Owns: parsing the `visual ...` lines of `ctest -L gpu -V` (run with R1UI_DIFF_SWEEP=1, which makes every
line end with the failing share at channel tolerances 0, 8, ... 128) and turning them into (1) a table
of every comparison against its current profile and (2) for each profile the frontier "channel
tolerance -> smallest failing share that lets every included comparison pass" and a proposed profile.
Why: tolerance values must follow measured renders, not guesses; the committed tests/reference/
tolerance.json is owned by the project owner, so the tool only proposes (docs/spec/tolerance_proposal.md)
and the visual suite can be run against a proposal with R1UI_TOLERANCE_FILE.
Callers: run by hand after a ctest -V log exists. Usage:
  python tools/spec/tolerance_proposal.py LOG [LOG ...] [--exclusions FILE] [--share-step 0.5] [--out FILE]
  [--write-json FILE]   writes the proposed tolerance.json (same layout as tests/reference/tolerance.json)
Exclusions file (JSON): {"<comparison name> <theme>": "reason it is not a visual match", ...}; those
comparisons are listed but not used to size a profile.
Input is untrusted text: lines that do not match are ignored, numbers are range checked.
"""
import argparse
import json
import math
import re
import sys

LINE = re.compile(
    r"visual (?P<name>\S.*?)\s+(?P<theme>dark|light)\s+(?P<profile>\w+)\s+"
    r"(?:(?P<w>\d+)x(?P<h>\d+)|region (?P<area>\d+) px:)\s*failing (?P<fail>\d+) \((?P<pct>[\d.]+)%(?: of region)?\) "
    r"max (?P<max>\d+) mean (?P<mean>[\d.]+) (?P<verdict>PASS|FAIL)(?: sweep (?P<sweep>[\d.: ]*))?")

CURRENT = {
    "default": (8, 0.2),
    "flat": (2, 0.0),
    "text": (48, 3.0),
    "icons": (32, 2.0),
    "screen": (32, 3.0),
}
STEPS = list(range(0, 129, 8))


def parse(paths):
    rows = {}
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as handle:
            for text in handle:
                text = re.sub(r"^\d+:\s*", "", text.strip())
                m = LINE.search(text)
                if not m or not m.group("sweep"):
                    continue
                sweep = {}
                for t, pct in re.findall(r"(\d+):([\d.]+)", m.group("sweep")):
                    if int(t) in STEPS and 0.0 <= float(pct) <= 100.0:
                        sweep[int(t)] = float(pct)
                if len(sweep) != len(STEPS):
                    continue
                key = (m.group("name"), m.group("theme"), m.group("profile"))
                rows[key] = {
                    "name": m.group("name"), "theme": m.group("theme"), "profile": m.group("profile"),
                    "fail": float(m.group("pct")), "max": int(m.group("max")), "mean": float(m.group("mean")),
                    "verdict": m.group("verdict"), "sweep": sweep,
                }
    return list(rows.values())


def round_up(value, step):
    return math.ceil(value / step - 1e-9) * step


def frontier(rows):
    """For each tolerance step: the largest failing share (percent) over `rows`."""
    return {t: max((r["sweep"][t] for r in rows), default=0.0) for t in STEPS}


def propose(front, step, floor_share):
    """Smallest tolerance whose required share is within one step of the share at the next higher
    tolerance steps, so the profile sits at the knee of the frontier, not on its flat tail."""
    best = None
    for t in STEPS:
        share = round_up(front[t], step)
        # Stop at the first tolerance after which raising it further buys less than one share step
        # over the next two tolerance steps.
        later = [round_up(front[u], step) for u in STEPS if t < u <= t + 16]
        if later and share - min(later) <= step + 1e-9:
            best = (t, max(share, floor_share))
            break
    if best is None:
        t = STEPS[-1]
        best = (t, max(round_up(front[t], step), floor_share))
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--exclusions")
    ap.add_argument("--share-step", type=float, default=0.5)
    ap.add_argument("--out")
    ap.add_argument("--write-json")
    args = ap.parse_args()
    rows = parse(args.logs)
    excluded = {}
    if args.exclusions:
        with open(args.exclusions, encoding="utf-8") as handle:
            excluded = json.load(handle)
    out = []
    emit = out.append
    emit("## Every comparison against its current profile\n")
    emit("| comparison | theme | profile | failing | max | mean | verdict | counted |")
    emit("|---|---|---|---|---|---|---|---|")
    for r in sorted(rows, key=lambda r: (r["profile"], -r["fail"])):
        key = r["name"] + " " + r["theme"]
        emit(f"| {r['name']} | {r['theme']} | {r['profile']} | {r['fail']:.2f}% | {r['max']} | {r['mean']:.2f} | {r['verdict']} | {'no: ' + excluded[key] if key in excluded else 'yes'} |")
    emit("")
    proposal = {}
    emit("## Frontier and proposal per profile\n")
    emit("Frontier: for a channel tolerance t, the largest failing share (percent of pixels) over the counted comparisons of the profile.\n")
    for profile in ("default", "flat", "text", "icons", "screen"):
        members = [r for r in rows if r["profile"] == profile]
        counted = [r for r in members if r["name"] + " " + r["theme"] not in excluded]
        if not members:
            continue
        front = frontier(counted)
        emit(f"### {profile}: {len(members)} comparisons, {len(counted)} counted, current {CURRENT[profile][0]} channels / {CURRENT[profile][1]}%\n")
        emit("| t | " + " | ".join(str(t) for t in STEPS) + " |")
        emit("|---|" + "---|" * len(STEPS))
        emit("| share | " + " | ".join(f"{front[t]:.2f}" for t in STEPS) + " |")
        t, share = propose(front, args.share_step, 0.0)
        proposal[profile] = (t, share)
        emit(f"\nProposed: **{t} channels, {share:.1f}%**.\n")
    if args.write_json:
        data = {"default": {"channelTolerance": proposal["default"][0], "maxFailingFraction": round(proposal["default"][1] / 100.0, 6)}, "profiles": {}}
        for profile, (t, share) in proposal.items():
            if profile != "default":
                data["profiles"][profile] = {"channelTolerance": t, "maxFailingFraction": round(share / 100.0, 6)}
        with open(args.write_json, "w", encoding="utf-8") as handle:
            json.dump(data, handle, indent=2)
            handle.write("\n")
    text = "\n".join(out) + "\n"
    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
