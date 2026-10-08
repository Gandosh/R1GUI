# Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
"""License boundary audit (CTest fast tier).

Owns: the rule that no copyleft or reference-derived code enters first-party modules.
Checks, over source/, tests/, examples/ and tools/:
  1. No file contains a GPL/LGPL/AGPL SPDX identifier or licence header.
  2. No build file or include path references reference/ or scratch/ (never build inputs).
  3. Every first-party C/C++ source/header starts with the project copyright line.
Exit code 0 = pass. Violations are listed with file and line.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCAN_DIRS = ["source", "tests", "examples", "tools"]
CODE_SUFFIXES = {".h", ".hpp", ".cpp", ".c", ".cc", ".inl"}
TEXT_SUFFIXES = CODE_SUFFIXES | {".txt", ".cmake", ".json", ".py", ".cmd", ".md"}
COPYLEFT = re.compile(r"(GPL|LGPL|AGPL)[-_ ]?(v?\d|-or-later|-only)|GNU (Lesser |Affero )?General Public", re.I)
FORBIDDEN_PATH = re.compile(r"(?<![\w.])(\.\./)*(reference|scratch)[/\\]", re.I)
HEADER = "// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary."
SELF = pathlib.Path(__file__).resolve()


def main() -> int:
    problems = []
    for d in SCAN_DIRS:
        base = ROOT / d
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if not path.is_file() or path.resolve() == SELF:
                continue
            if path.suffix.lower() not in TEXT_SUFFIXES and path.name != "CMakeLists.txt":
                continue
            rel = path.relative_to(ROOT)
            try:
                lines = path.read_text(encoding="utf-8").splitlines()
            except UnicodeDecodeError:
                problems.append(f"{rel}: not valid UTF-8")
                continue
            if path.suffix in CODE_SUFFIXES and (not lines or lines[0] != HEADER):
                problems.append(f"{rel}:1: missing project copyright header")
            for n, line in enumerate(lines, 1):
                if COPYLEFT.search(line):
                    problems.append(f"{rel}:{n}: copyleft licence text: {line.strip()[:80]}")
                is_build = path.name == "CMakeLists.txt" or path.suffix in {".cmake", ".cmd"}
                if (is_build or line.lstrip().startswith("#include")) and FORBIDDEN_PATH.search(line):
                    problems.append(f"{rel}:{n}: references reference/ or scratch/: {line.strip()[:80]}")
    for p in problems:
        print(p)
    print(f"license_boundary_audit: {'FAIL' if problems else 'ok'} ({len(problems)} problems)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
