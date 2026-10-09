// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: behavior oracle for TextEditor: grapheme-safe caret and deletion, selection rules,
//   clipboard hooks, undo grouping, max length, sanitizing, composition, read-only mode, and
//   layout-driven caret x, hit-testing and scrolling.
// Callers: CTest (label fast). Exit code 0 = pass. Random-operation fuzzing is in
//   editor_fuzz_test.cpp.
#include <chrono>
#include <cmath>
#include <optional>
#include <string>

#include "TestSupport.h"
#include "r1ui/text/Grapheme.h"
#include "r1ui/text/TextEditor.h"
#include "r1ui/text/Utf8.h"

using namespace r1ui::text;
using namespace r1ui::text::testing;

namespace {

const std::string kFamily = utf8({0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467});  // 18 bytes
const std::string kFlagUs = utf8({0x1F1FA, 0x1F1F8});
const std::string kFlagFr = utf8({0x1F1EB, 0x1F1F7});
const std::string kAcute = utf8({'e', 0x301});

bool onBoundary(const TextEditor& e) {
  return isGraphemeBoundary(e.text(), e.caret()) && isGraphemeBoundary(e.text(), e.anchor());
}

void testCaretAndDeletion() {
  TextEditor e;
  expect(e.text().empty() && e.caret() == 0 && !e.hasSelection(), "starts empty");
  expect(e.insertText("abc") == 3 && e.text() == "abc" && e.caret() == 3, "typing");

  e.move(Motion::Left, false);
  expect(e.caret() == 2 && e.anchor() == 2, "left");
  e.move(Motion::LineStart, false);
  expect(e.caret() == 0, "home");
  e.move(Motion::LineEnd, false);
  expect(e.caret() == 3, "end");
  e.move(Motion::Right, false);
  expect(e.caret() == 3, "right at end stays");

  // Whole clusters move and delete as one unit.
  e.setText("a" + kFamily + "b");
  e.move(Motion::Left, false);
  e.move(Motion::Left, false);
  expect(e.caret() == 1, "left skips the whole ZWJ family");
  e.move(Motion::Right, false);
  expect(e.caret() == 1 + kFamily.size() && onBoundary(e), "right lands after the family");
  expect(e.deleteBackward() && e.text() == "ab", "backspace removes the whole family");

  e.setText(kFlagUs + kFlagFr);
  e.move(Motion::LineStart, false);
  expect(e.deleteForward() && e.text() == kFlagFr, "delete removes one flag, not half");
  e.setText(kAcute + "x");
  e.move(Motion::LineStart, false);
  e.move(Motion::Right, false);
  expect(e.caret() == kAcute.size(), "caret cannot enter e + acute");
  e.move(Motion::Left, false);
  expect(e.caret() == 0, "left leaves e + acute in one step");
  expect(e.deleteForward() && e.text() == "x", "delete removes base and mark together");

  // Hangul jamo and emoji with modifiers.
  e.setText(utf8({0x1112, 0x1161, 0x11AB, 'x'}));
  e.move(Motion::LineStart, false);
  e.move(Motion::Right, false);
  expect(e.caret() == 9, "Hangul L V T is one cell");

  // Inserting a regional indicator between flags re-pairs them; the caret must still be on a boundary.
  e.setText(kFlagUs + kFlagFr);
  e.setSelection(kFlagUs.size(), kFlagUs.size());
  e.insertText(utf8({0x1F1E6}));
  expect(onBoundary(e) && e.caret() == 16, "caret snaps to the end of the re-paired cluster");

  // setSelection snaps arbitrary offsets.
  e.setText(kAcute + "xyz");
  e.setSelection(1, 99);
  expect(e.anchor() == 0 && e.caret() == e.text().size(), "setSelection snaps and clamps");
  e.setSelection(2, 2);
  expect(onBoundary(e), "setSelection inside a cluster snaps to a boundary");
}

void testSelection() {
  TextEditor e;
  e.setText("hello world");
  e.setSelection(5, 2);
  expect(e.selection() == (TextRange{2, 5}) && e.hasSelection() && e.anchor() == 5 && e.caret() == 2, "normalized selection");
  e.move(Motion::Left, false);
  expect(e.caret() == 2 && !e.hasSelection(), "left collapses to the start");
  e.setSelection(2, 5);
  e.move(Motion::Right, false);
  expect(e.caret() == 5 && !e.hasSelection(), "right collapses to the end");
  e.setSelection(2, 5);
  e.move(Motion::LineEnd, false);
  expect(e.caret() == 11 && !e.hasSelection(), "end collapses the selection");

  e.move(Motion::LineStart, false);
  e.move(Motion::Right, true);
  e.move(Motion::Right, true);
  expect(e.selection() == (TextRange{0, 2}) && e.anchor() == 0, "shift+right extends");
  e.move(Motion::Left, true);
  expect(e.selection() == (TextRange{0, 1}), "shift+left shrinks");
  e.move(Motion::WordRight, true);
  expect(e.selection() == (TextRange{0, 6}), "ctrl+shift+right extends by a word");
  e.move(Motion::LineEnd, true);
  expect(e.selection() == (TextRange{0, 11}), "shift+end");
  e.move(Motion::LineStart, true);
  expect(!e.hasSelection(), "shift+home back to the anchor");

  e.selectAll();
  expect(e.selection() == (TextRange{0, 11}), "select all");
  expect(e.insertText("x") == 1 && e.text() == "x" && e.caret() == 1, "typing replaces the selection");

  e.setText("hello world");
  e.pointerPress(8, 2, false);
  expect(e.selection() == (TextRange{6, 11}), "double click selects the word");
  e.pointerPress(5, 2, false);
  expect(e.selection() == (TextRange{5, 6}), "double click on a space selects the space run");
  e.pointerPress(3, 3, false);
  expect(e.selection() == (TextRange{0, 11}), "triple click selects all");
  e.pointerPress(4, 1, false);
  expect(e.caret() == 4 && !e.hasSelection(), "single click places the caret");
  e.pointerPress(9, 1, true);
  expect(e.selection() == (TextRange{4, 9}) && e.anchor() == 4, "shift-click extends from the anchor");
  e.pointerPress(2, 1, true);
  expect(e.selection() == (TextRange{2, 4}), "drag backwards past the anchor flips the selection");

  e.setText("");
  e.selectAll();
  e.pointerPress(0, 2, false);
  expect(!e.hasSelection(), "double click in empty text");
}

void testWordOps() {
  TextEditor e;
  e.setText("hello world");
  expect(e.deleteWordBackward() && e.text() == "hello ", "ctrl+backspace deletes the last word");
  expect(e.deleteWordBackward() && e.text().empty(), "again deletes the rest");
  expect(!e.deleteWordBackward(), "nothing left to delete");
  e.setText("hello world");
  e.move(Motion::LineStart, false);
  expect(e.deleteWordForward() && e.text() == "world", "ctrl+delete removes the word and its space");
  e.setText("a.b");
  e.move(Motion::WordLeft, false);
  expect(e.caret() == 2, "ctrl+left lands before the last word");
  e.move(Motion::WordLeft, false);
  expect(e.caret() == 1, "punctuation is its own stop");
  e.move(Motion::WordRight, false);
  expect(e.caret() == 2, "ctrl+right");
}

void testSanitizing() {
  TextEditor e;
  const std::string messy("a\r\nb\tc\0d\x01\x7F", 9);
  expect(e.insertText(messy) == 4 && e.text() == "abcd", "controls (incl. NUL, CR, LF, TAB) are dropped");
  e.setText("");
  expect(e.insertText("x\xFF" "y") == 5 && e.text() == utf8({'x', 0xFFFD, 'y'}), "invalid UTF-8 is replaced");
  e.setText("");
  expect(e.insertText("\xED\xA0\x80") == 9 && isValidUtf8(e.text()), "lone surrogate bytes become U+FFFD");
  e.setText("");
  expect(e.insertText(utf8({0x2028, 0x2029, 0x85})) == 0 && e.text().empty(), "line separators dropped");
  expect(e.insertText("") == 0, "empty insert");
  e.setText("a\nb\xFF");
  expect(e.text() == utf8({'a', 'b', 0xFFFD}) && e.caret() == e.text().size(), "setText sanitizes and parks the caret at the end");
}

void testMaxLength() {
  EditorConfig cfg;
  cfg.maxBytes = 5;
  TextEditor e(cfg);
  expect(e.insertText("abcdefgh") == 5 && e.text() == "abcde", "cut to the limit");
  expect(e.insertText("x") == 0 && e.text() == "abcde", "full: nothing more");
  e.setSelection(1, 3);
  expect(e.insertText("XYZ") == 2 && e.text() == "aXYde", "replacing a selection only counts the growth");

  EditorConfig four;
  four.maxBytes = 4;
  TextEditor m(four);
  expect(m.insertText(utf8({'a', 0xE9, 0x20AC})) == 3 && m.text() == utf8({'a', 0xE9}), "never cut inside a character");
  EditorConfig ten;
  ten.maxBytes = 10;
  TextEditor f(ten);
  expect(f.insertText(kFamily) == 0 && f.text().empty(), "never cut inside a cluster (cluster larger than the limit)");
  f.setText(kFamily + kFamily);
  expect(f.text().empty(), "setText applies the same rule");

  TextEditor g;
  g.setText("hello");
  g.setMaxBytes(2);
  expect(g.text() == "hello" && g.maxBytes() == 2, "lowering the limit keeps existing text");
  expect(g.insertText("x") == 0, "but blocks growth");
  expect(g.deleteBackward() && g.text() == "hell", "deleting still works");
  g.setMaxBytes(0);
  expect(g.maxBytes() == 1, "limit clamps to at least 1");
  g.setMaxBytes(kMaxAllowedBytes * 4);
  expect(g.maxBytes() == kMaxAllowedBytes, "limit clamps to the hard cap");
}

void testBigPaste() {
  std::string unit = std::string("abc ") + utf8({0xE9, 0x20AC, 0x1F600}) + " ";
  std::string clip;
  while (clip.size() + unit.size() <= 1000000) clip += unit;
  ClipboardCallbacks cb;
  cb.read = [&]() { return std::optional<std::string>(clip); };
  TextEditor e;
  e.setClipboard(cb);
  const auto t0 = std::chrono::steady_clock::now();
  expect(e.paste() && e.text() == clip, "1 MB paste accepted whole");
  e.move(Motion::LineStart, false);
  e.move(Motion::Right, true);
  e.move(Motion::WordRight, true);
  expect(onBoundary(e) && e.deleteBackward(), "editing a 1 MB text works");
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  expect(ms < 3000.0, "1 MB paste and a few edits finish quickly");
  expect(e.undo() && e.undo() && e.text().empty(), "undoing the edit and the 1 MB paste");

  EditorConfig small;
  small.maxBytes = 100000;
  TextEditor s(small);
  s.setClipboard(cb);
  expect(s.paste() && s.text().size() <= 100000 && isValidUtf8(s.text()) && onBoundary(s) && s.text().size() > 99000,
         "paste above the limit is cut at a boundary");

  std::string noise(1000000, '\0');
  std::uint32_t state = 5;
  for (char& c : noise) {
    state = state * 1664525u + 1013904223u;
    c = static_cast<char>(state >> 24);
  }
  cb.read = [&]() { return std::optional<std::string>(noise); };
  TextEditor n;
  n.setClipboard(cb);
  n.paste();
  expect(isValidUtf8(n.text()) && onBoundary(n) && n.text().size() <= kDefaultMaxBytes, "1 MB of random bytes pastes as valid text");

  std::string huge(20 * 1024 * 1024, 'a');
  cb.read = [&]() { return std::optional<std::string>(std::move(huge)); };
  TextEditor h;
  h.setClipboard(cb);
  expect(h.paste() && h.text().size() == kDefaultMaxBytes, "20 MB clipboard cut to the 1 MiB default limit");
}

void testUndo() {
  TextEditor e;
  for (const char c : std::string("hello")) e.insertText(std::string(1, c));
  expect(e.canUndo() && !e.canRedo(), "history exists");
  expect(e.undo() && e.text().empty() && e.caret() == 0, "typing is one undo step");
  expect(!e.canUndo() && e.canRedo(), "nothing left to undo");
  expect(e.redo() && e.text() == "hello" && e.caret() == 5, "redo restores the text and caret");
  expect(!e.redo(), "redo exhausted");

  e.move(Motion::Left, false);
  e.insertText("X");
  e.insertText("Y");
  expect(e.text() == "hellXYo", "typing after moving");
  expect(e.undo() && e.text() == "hello", "moving the caret starts a new group");
  expect(e.undo() && e.text().empty(), "older group");

  e.setText("");
  e.insertText("abc");
  e.breakUndoGroup();
  e.insertText("def");
  expect(e.undo() && e.text() == "abc", "explicit break splits groups");
  e.insertText("Z");
  expect(!e.canRedo() && e.text() == "abcZ", "a new edit clears redo");

  e.setText("abcdef");
  expect(e.deleteBackward() && e.deleteBackward() && e.deleteBackward() && e.text() == "abc", "three backspaces");
  expect(e.undo() && e.text() == "abcdef" && e.caret() == 6, "consecutive backspaces are one step");
  e.move(Motion::LineStart, false);
  expect(e.deleteForward() && e.deleteForward() && e.text() == "cdef", "two deletes");
  expect(e.undo() && e.text() == "abcdef" && e.caret() == 0, "consecutive forward deletes are one step");

  // Selection replaced by typing: the whole group restores the selection.
  e.setText("hello world");
  e.setSelection(6, 11);
  e.insertText("a");
  e.insertText("b");
  expect(e.text() == "hello ab", "replace selection then keep typing");
  expect(e.undo() && e.text() == "hello world" && e.selection() == (TextRange{6, 11}), "one step restores text and selection");
  expect(e.redo() && e.text() == "hello ab", "redo");

  // Clipboard operations are their own steps.
  std::string board;
  ClipboardCallbacks cb;
  cb.write = [&](std::string_view s) { board.assign(s); };
  cb.read = [&]() { return std::optional<std::string>(board); };
  e.setClipboard(cb);
  e.setText("one two");
  e.insertText("!");
  e.setSelection(0, 3);
  expect(e.cut() && board == "one" && e.text() == " two!", "cut");
  e.move(Motion::LineEnd, false);
  expect(e.paste() && e.text() == " two!one", "paste");
  e.insertText("?");
  expect(e.undo() && e.text() == " two!one", "typing after paste is its own step");
  expect(e.undo() && e.text() == " two!", "paste is one step");
  expect(e.undo() && e.text() == "one two!", "cut is one step");
  expect(e.selection() == (TextRange{0, 3}), "undoing a cut restores the selection");

  // History is cleared by setText and bounded.
  e.setText("fresh");
  expect(!e.canUndo(), "setText clears history");
  for (int i = 0; i < 3000; ++i) {
    e.insertText("a");
    e.breakUndoGroup();
  }
  int steps = 0;
  while (e.undo()) ++steps;
  expect(steps <= 1000 && steps > 900, "history is bounded to about 1000 steps");
}

void testClipboard() {
  TextEditor e;
  e.setText("abc");
  e.selectAll();
  expect(!e.copy() && !e.cut() && !e.paste(), "no callbacks: clipboard calls report false");
  std::string board;
  ClipboardCallbacks cb;
  cb.write = [&](std::string_view s) { board.assign(s); };
  cb.read = [&]() -> std::optional<std::string> { return std::nullopt; };
  e.setClipboard(cb);
  expect(e.copy() && board == "abc" && e.text() == "abc", "copy leaves the text");
  expect(!e.paste(), "empty clipboard");
  e.setSelection(1, 1);
  expect(!e.copy(), "nothing selected: nothing copied");
  cb.read = [&]() { return std::optional<std::string>("X\nY"); };
  e.setClipboard(cb);
  expect(e.paste() && e.text() == "aXYbc", "pasted newlines are dropped");

  EditorConfig ro;
  ro.readOnly = true;
  TextEditor r(ro);
  r.setText("fixed");
  r.setClipboard(cb);
  r.selectAll();
  expect(r.copy() && board == "fixed", "read-only can copy");
  expect(!r.cut() && !r.paste() && r.insertText("x") == 0 && !r.deleteBackward() && !r.deleteForward() &&
             !r.deleteWordBackward() && !r.undo() && !r.setPreedit("a", 1),
         "read-only blocks every mutation");
  expect(r.text() == "fixed", "read-only text unchanged");
  r.move(Motion::LineStart, true);
  expect(!r.hasSelection() || r.selection().begin == 0, "read-only can still move");
  r.setText("programmatic");
  expect(r.text() == "programmatic", "setText works in read-only mode");
  r.setReadOnly(false);
  expect(r.insertText("!") == 1, "editable again");
}

void testComposition() {
  TextEditor e;
  e.setText("ab");
  e.setSelection(1, 1);
  expect(e.setPreedit("ni", 2) && e.composing(), "start composing");
  expect(e.text() == "ab" && e.displayText() == "anib", "buffer untouched, display spliced");
  expect(e.preeditRange() == (TextRange{1, 3}) && e.displayCaret() == 3, "preedit region and caret");
  expect(e.setPreedit("nih", 1) && e.displayText() == "anihb" && e.displayCaret() == 2, "preedit update");
  e.cancelPreedit();
  expect(!e.composing() && e.displayText() == "ab" && !e.preeditRange(), "cancel");

  e.setPreedit(utf8({0x4F60, 0x597D}), 3);
  expect(e.commitPreedit() && !e.composing() && e.text() == "a" + utf8({0x4F60, 0x597D}) + "b" && e.caret() == 7,
         "commit inserts at the caret");
  expect(!e.commitPreedit(), "nothing to commit");
  expect(e.undo() && e.text() == "ab", "a commit is one undo step");

  // Starting composition over a selection replaces it.
  e.setText("hello");
  e.setSelection(1, 4);
  e.setPreedit("x", 1);
  expect(e.text() == "ho" && e.displayText() == "hxo", "selection removed when composition starts");
  e.cancelPreedit();
  expect(e.undo() && e.text() == "hello", "removal is undoable");

  // Other operations cancel composition.
  e.setPreedit("zz", 2);
  e.move(Motion::Left, false);
  expect(!e.composing(), "movement cancels composition");
  e.setPreedit("zz", 0);
  expect(e.insertText("q") == 1 && !e.composing(), "typing cancels composition");

  // Bad cursor and hostile preedit.
  e.setPreedit(utf8({0x20AC}), 2);
  expect(e.displayCaret() == e.caret(), "cursor inside a character snaps back");
  expect(e.setPreedit("", 0) && !e.composing(), "empty preedit cancels");
  std::string longPreedit(10000, 'k');
  e.setPreedit(longPreedit, 99999);
  expect(e.composing() && e.preeditRange()->end - e.preeditRange()->begin <= 4096, "preedit is bounded");
  e.cancelPreedit();
  EditorConfig tight;
  tight.maxBytes = 4;
  TextEditor t(tight);
  t.setText("abc");
  t.setPreedit("12345", 5);
  expect(t.displayText().size() <= 4, "preedit respects the length limit");
}

void testLayout() {
  FontLibrary lib;
  const FontHandle font = loadInter(lib, "Inter-Regular.ttf", 400);
  TextEditor e;
  e.setText("Rectangle tool");
  expect(!e.layoutCurrent() && !e.caretX() && e.hitTest(10.0f) == e.caret(), "no layout yet");
  expect(e.relayout(*font, 12).ok() && e.layoutCurrent(), "relayout");
  expect(e.textWidth() > 50.0f, "text width");
  expect(std::abs(*e.caretX() - e.textWidth()) < 1e-3f, "caret at the end sits at the text width");

  // Hit-testing a boundary's own x returns that boundary.
  bool roundTrip = true;
  std::size_t pos = 0;
  e.setSelection(0, 0);
  for (int i = 0; i <= 14; ++i) {
    e.setSelection(pos, pos);
    e.relayout(*font, 12);
    roundTrip = roundTrip && e.hitTest(*e.caretX()) == pos;
    pos = nextGraphemeBoundary(e.text(), pos);
  }
  expect(roundTrip, "hitTest(caretX) is the identity on boundaries");
  expect(e.hitTest(-50) == 0 && e.hitTest(1e6f) == e.text().size() && e.hitTest(std::nanf("")) == 0, "hit test clamps");
  e.setSelection(2, 6);
  e.relayout(*font, 12);
  const auto sx = e.selectionX();
  expect(sx && sx->first < sx->second && sx->first > 0, "selection extents");

  e.insertText("x");
  expect(!e.layoutCurrent(), "an edit invalidates the layout");

  // Clusters: the caret x for text with a combining mark has no stop inside it.
  e.setText(kAcute + kAcute);
  e.relayout(*font, 14);
  e.setSelection(0, 0);
  e.move(Motion::Right, false);
  expect(e.caret() == kAcute.size(), "caret skips the cluster");

  // Scrolling.
  e.setText("The quick brown fox jumps over the lazy dog");
  e.relayout(*font, 12);
  const float total = e.textWidth();
  expect(e.ensureCaretVisible(60.0f, 2.0f) > 0.0f, "caret at the end scrolls right");
  float scroll = e.scrollX();
  expect(std::abs((*e.caretX() - scroll) - 58.0f) < 1e-3f, "caret sits at the right margin");
  expect(scroll <= total + 2.0f - 60.0f + 1e-3f, "scroll never exceeds the content");
  e.move(Motion::LineStart, false);
  e.relayout(*font, 12);
  expect(e.ensureCaretVisible(60.0f, 2.0f) == 0.0f, "caret at the start scrolls back to 0");
  e.setSelection(20, 20);
  e.relayout(*font, 12);
  e.ensureCaretVisible(60.0f, 2.0f);
  const float x = *e.caretX() - e.scrollX();
  expect(x >= 2.0f - 1e-3f && x <= 58.0f + 1e-3f, "caret inside the field after scrolling");
  const float before = e.scrollX();
  expect(e.ensureCaretVisible(0.0f) == before && e.ensureCaretVisible(std::nanf("")) == before &&
             e.ensureCaretVisible(-3.0f) == before,
         "invalid field widths leave the scroll alone");
  e.setText("hi");
  e.relayout(*font, 12);
  expect(e.ensureCaretVisible(300.0f) == 0.0f, "short text never scrolls");
  e.setText("");
  e.relayout(*font, 12);
  expect(e.ensureCaretVisible(30.0f) == 0.0f && e.textWidth() == 0.0f, "empty text");

  // Composition shows in the layout and hit-tests inside it map to the caret.
  e.setText("ab");
  e.setSelection(1, 1);
  e.setPreedit("WWW", 3);
  e.relayout(*font, 12);
  expect(e.textWidth() > 20.0f, "layout includes the preedit");
  expect(e.hitTest(*e.caretX() - 8.0f) == 1, "a hit inside the preedit maps to the caret");
  expect(e.hitTest(e.textWidth() + 100) == 2, "hit after the preedit maps back to the text end");
  expect(e.hitTest(0) == 0, "hit before the preedit");

  // Errors keep the editor consistent.
  expect(!e.relayout(*font, std::nanf("")).ok() && !e.layoutCurrent(), "bad size: error and stale layout");
}

void testMove() {
  TextEditor a;
  a.setText("moved");
  TextEditor b(std::move(a));
  expect(b.text() == "moved" && b.caret() == 5, "move construction keeps state");
  TextEditor c;
  c = std::move(b);
  expect(c.text() == "moved", "move assignment");
}

}  // namespace

int main() {
  testCaretAndDeletion();
  testSelection();
  testWordOps();
  testSanitizing();
  testMaxLength();
  testBigPaste();
  testUndo();
  testClipboard();
  testComposition();
  testLayout();
  testMove();
  return finish("editor");
}
