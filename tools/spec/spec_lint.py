# Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
"""Lint for behavior-only interaction specs (CTest fast tier).

Owns: the mechanical part of the rule that specs derived from reading a reference editor's source
contain behavior only: no code, no identifiers, no file names, no class or function names, no quoted
comments. A reviewer still reads the specs; this catches the obvious leaks.
Checks every Markdown file under docs/spec/interaction/ for:
  - code fences and inline code spans that contain code-like text (calls, scope operators, arrows);
  - identifier-shaped words: CamelCase or snake_case words with an engine-style type prefix (F, S, U, A, E, I, T
    followed by a capital) and any word with an internal capital run such as lowerUpper;
  - source file names and paths (.h, .cpp, .cs, .inl, Engine/Source, Runtime/, Editor/ paths);
  - the reference framework's own names: Unreal, Slate, UMG, Kismet, Blueprint class names, and macros (UE_, UCLASS);
  - quoted multi-word text and source-commentary phrasing (the code, I read, a bug, enum, gamepad, ...);
  - lines longer than 400 characters (pasted text) and verbatim comment markers (//, /*).
Exceptions: tools/spec/spec_lint_allow.txt lists exact words that are ordinary UI vocabulary (one per line).
Usage: python tools/spec/spec_lint.py [files...]   (defaults to every spec). Exit 0 = pass, 1 = violations.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC_DIR = ROOT / "docs" / "spec" / "interaction"
ALLOW_FILE = pathlib.Path(__file__).resolve().parent / "spec_lint_allow.txt"

PREFIXED_TYPE = re.compile(r"\b[FSUAEIT][A-Z][a-z0-9]+(?:[A-Z][A-Za-z0-9]*)+\b")
CAMEL_INNER = re.compile(r"\b[a-z]+[A-Z][A-Za-z0-9]*\b")
SNAKE = re.compile(r"\b[a-z]+(?:_[a-z0-9]+)+\b")
SCOPE = re.compile(r"::|->|\w\(\)|\w\([^)]*\)\s*;|\bnullptr\b|#include|#define")
FILES = re.compile(r"\b\w+\.(?:h|hpp|cpp|cs|inl|uasset|umap|ini)\b|Engine/(?:Source|Plugins|Config)|Runtime/|Editor/\w+")
FRAMEWORK = re.compile(r"\b(?:Unreal|Slate|UMG|Kismet|Blueprint|UCLASS|UPROPERTY|UFUNCTION|GENERATED_BODY)\b|\bUE_\w+")
# Quoted multi-word text is usually a user-visible string copied from the source: describe the function instead.
QUOTED = re.compile(r'"[^"]*[A-Za-z]+ +[A-Za-z]+[^"]*"|' + chr(0x201c) + r'[^' + chr(0x201d) + r']*[A-Za-z]+ +[A-Za-z]+[^' + chr(0x201d) + r']*' + chr(0x201d) + r"|'[A-Z][a-z]+ [A-Za-z ]+'")
# First-person or source-commentary phrasing and source-specific vocabulary.
COMMENTARY = re.compile(r"(?:the code|code path|I read|I found|I could|I inferred|arithmetic|a defect|a bug|enum|delegate|callback|gamepad|analog cursor|widget inspector|Nomad|command bundle|Major tabs?|Minor tabs?|source code|implementation detail)", re.I)
COMMENTS = re.compile(r"(?:^|\s)(?://|/\*|\*/)")


def load_allow():
    if not ALLOW_FILE.exists():
        return set()
    return {w.strip() for w in ALLOW_FILE.read_text(encoding="utf-8").splitlines() if w.strip() and not w.startswith("#")}


def lint_file(path, allow):
    problems = []
    in_fence = False
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if line.strip().startswith("```"):
            problems.append((n, "code fence"))
            in_fence = not in_fence
            continue
        if in_fence:
            problems.append((n, "inside code fence"))
            continue
        if len(line) > 400:
            problems.append((n, "very long line (pasted text?)"))
        stripped = re.sub(r"https?://\S+", "", line)
        for pattern, what in ((SCOPE, "code-like syntax"), (FILES, "source file or path"),
                              (FRAMEWORK, "reference framework name"), (COMMENTS, "comment marker"),
                              (QUOTED, "quoted multi-word text (user-visible string?)"),
                              (COMMENTARY, "source commentary or source-specific term")):
            m = pattern.search(stripped)
            if m:
                problems.append((n, f"{what}: {m.group(0).strip()}"))
        for pattern, what in ((PREFIXED_TYPE, "prefixed identifier"), (CAMEL_INNER, "camelCase identifier"),
                              (SNAKE, "snake_case identifier")):
            for m in pattern.finditer(stripped):
                if m.group(0) not in allow:
                    problems.append((n, f"{what}: {m.group(0)}"))
    return problems


def main(argv):
    allow = load_allow()
    files = [pathlib.Path(a) for a in argv] or sorted(SPEC_DIR.glob("*.md"))
    total = 0
    for f in files:
        for n, what in lint_file(f, allow):
            print(f"{f.name}:{n}: {what}")
            total += 1
    print(f"spec_lint: {'FAIL' if total else 'ok'} ({total} problems in {len(files)} files)")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
