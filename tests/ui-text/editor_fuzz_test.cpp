// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: randomized stress of TextEditor: 10,000 random operations per run (several seeds and
//   length limits), checking after every step that the editor invariants hold: caret and anchor
//   within bounds and on grapheme boundaries, text valid UTF-8 and within the length limit,
//   selection normalized, preedit region inside the display text; plus an undo/redo round trip
//   around random edits.
// Callers: CTest (label fast). Exit code 0 = pass. Seeds are fixed, so a failure reproduces.
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/text/Grapheme.h"
#include "r1ui/text/TextEditor.h"
#include "r1ui/text/Utf8.h"

using namespace r1ui::text;
using namespace r1ui::text::testing;

namespace {

class Rng {
 public:
  explicit Rng(std::uint32_t seed) : state_(seed) {}
  std::uint32_t next() {
    state_ = state_ * 1664525u + 1013904223u;
    return state_ >> 8;
  }
  std::size_t below(std::size_t n) { return n == 0 ? 0 : next() % n; }

 private:
  std::uint32_t state_;
};

const std::vector<std::string>& pool() {
  static const std::vector<std::string> p = {
      "a", "b", " ", "hello", "world ", ".", "Rectangle", utf8({0xE9}), utf8({'e', 0x301}), utf8({0x301}),
      utf8({0x20AC}), utf8({0x1F600}), utf8({0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467}), utf8({0x1F1FA, 0x1F1F8}),
      utf8({0x1F1E6}), utf8({0x200D}), utf8({0xFE0F}), utf8({0x1112, 0x1161, 0x11AB}), utf8({0x11A8}), utf8({0xAC00}),
      utf8({0x0915, 0x093E}), "\xFF", "\xE2\x82", "\xED\xA0\x80", std::string("\0", 1), "\r\n", "\t", "x\ny",
      std::string(300, 'z'), utf8({0x2028}), utf8({0x5E9, 0x5DC, 0x5D5, 0x5DD})};
  return p;
}

void checkInvariants(const TextEditor& e, std::size_t maxBytes, const char* where) {
  const std::string& t = e.text();
  const bool ok = e.caret() <= t.size() && e.anchor() <= t.size() && isValidUtf8(t) && t.size() <= maxBytes &&
                  isGraphemeBoundary(t, e.caret()) && isGraphemeBoundary(t, e.anchor()) &&
                  e.selection().begin <= e.selection().end && e.selection().end <= t.size() &&
                  e.selection().begin == std::min(e.caret(), e.anchor());
  if (!ok) {
    std::fprintf(stderr, "invariant broken after %s: size %zu caret %zu anchor %zu\n", where, t.size(), e.caret(), e.anchor());
    expect(false, "editor invariants");
    std::exit(1);
  }
  const std::string& shown = e.displayText();
  expect(isValidUtf8(shown), "display text valid UTF-8");
  if (const auto pr = e.preeditRange()) {
    expect(pr->begin <= pr->end && pr->end <= shown.size() && e.composing(), "preedit range inside the display text");
    expect(e.displayCaret() >= pr->begin && e.displayCaret() <= pr->end, "display caret inside the preedit");
  } else {
    expect(!e.composing() && shown == t && e.displayCaret() == e.caret(), "no composition: display equals text");
  }
}

void runFuzz(std::uint32_t seed, std::size_t maxBytes, FontLibrary& lib) {
  const FontHandle font = loadInter(lib, "Inter-Regular.ttf", 400);
  EditorConfig cfg;
  cfg.maxBytes = maxBytes;
  TextEditor e(cfg);
  Rng rng(seed);
  std::string board;
  ClipboardCallbacks cb;
  cb.write = [&](std::string_view s) {
    board.assign(s);
    return true;
  };
  cb.read = [&]() -> std::optional<std::string> {
    if (board.empty()) return std::nullopt;
    return board;
  };
  e.setClipboard(cb);

  for (int step = 0; step < 10000; ++step) {
    const std::string before = e.text();
    const std::size_t op = rng.below(30);
    const char* name = "op";
    bool roundTrip = false;  // an edit whose undo must restore `before` exactly
    switch (op) {
      case 0: case 1: case 2: case 3:
        e.breakUndoGroup();
        e.insertText(pool()[rng.below(pool().size())]);
        roundTrip = true;
        name = "insertText";
        break;
      case 4:
        e.breakUndoGroup();
        e.deleteBackward();
        roundTrip = true;
        name = "deleteBackward";
        break;
      case 5:
        e.breakUndoGroup();
        e.deleteForward();
        roundTrip = true;
        name = "deleteForward";
        break;
      case 6:
        e.deleteWordBackward();
        name = "deleteWordBackward";
        break;
      case 7:
        e.deleteWordForward();
        name = "deleteWordForward";
        break;
      case 8: case 9: case 10: case 11:
        e.move(static_cast<Motion>(rng.below(6)), rng.below(2) == 0);
        name = "move";
        break;
      case 12:
        e.selectAll();
        name = "selectAll";
        break;
      case 13:
        e.selectWordAt(rng.below(e.text().size() + 3));
        name = "selectWordAt";
        break;
      case 14:
        e.pointerPress(rng.below(e.text().size() + 3), static_cast<int>(1 + rng.below(4)), rng.below(2) == 0);
        name = "pointerPress";
        break;
      case 15:
        e.setSelection(rng.below(e.text().size() + 5), rng.below(e.text().size() + 5));
        name = "setSelection";
        break;
      case 16:
        e.copy();
        name = "copy";
        break;
      case 17:
        e.breakUndoGroup();
        e.cut();
        roundTrip = true;
        name = "cut";
        break;
      case 18:
        e.breakUndoGroup();
        e.paste();
        roundTrip = true;
        name = "paste";
        break;
      case 19:
        e.undo();
        name = "undo";
        break;
      case 20:
        e.redo();
        name = "redo";
        break;
      case 21:
        e.setPreedit(pool()[rng.below(pool().size())], rng.below(8));
        name = "setPreedit";
        break;
      case 22:
        e.commitPreedit();
        name = "commitPreedit";
        break;
      case 23:
        e.cancelPreedit();
        name = "cancelPreedit";
        break;
      case 24:
        e.setReadOnly(rng.below(4) == 0);
        name = "setReadOnly";
        break;
      case 25:
        if (rng.below(8) == 0) e.setText(pool()[rng.below(pool().size())]);
        name = "setText";
        break;
      case 26:
        if (e.relayout(*font, 12).ok()) {
          const float w = static_cast<float>(10 + rng.below(100));
          e.ensureCaretVisible(w, 2.0f);
          const std::size_t hit = e.hitTest(static_cast<float>(rng.below(300)) - 20.0f);
          expect(hit <= e.text().size() && isGraphemeBoundary(e.text(), hit), "hit test lands on a boundary");
        }
        name = "layout";
        break;
      default:
        e.insertText(pool()[rng.below(pool().size())]);
        name = "insertText (grouped)";
        break;
    }
    checkInvariants(e, maxBytes, name);

    if (roundTrip && !e.composing() && e.text() != before && !e.readOnly()) {
      const std::string after = e.text();
      expect(e.undo() && e.text() == before, "undo restores the exact previous text");
      checkInvariants(e, maxBytes, "undo");
      expect(e.redo() && e.text() == after, "redo restores the edited text");
      checkInvariants(e, maxBytes, "redo");
    }
    if (step % 1500 == 999) e.setText("");  // keep texts short enough for the boundary checks to stay fast
  }
}

}  // namespace

int main() {
  FontLibrary lib;
  for (const std::uint32_t seed : {1u, 2u, 3u, 4u}) {
    runFuzz(seed, kDefaultMaxBytes, lib);
    runFuzz(seed * 977u, 64, lib);
    runFuzz(seed * 7919u, 16, lib);
  }
  return finish("editor_fuzz");
}
