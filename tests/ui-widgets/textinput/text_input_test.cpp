// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for TextInput and the LineEditor under it: typing, selection by keyboard and
//   pointer (double click word, triple click all, shift-click, drag), clipboard copy / cut / paste and
//   its failure modes, undo and redo, the commit / cancel protocol (Enter, blur, Escape order of spec 01
//   rule 29), grapheme-counted max length, read-only, mixed and bound states, the clear button, IME
//   preedit hooks, horizontal scrolling, caret blink frames (regression for the continuous-frames
//   foundation fix), and hostile cases: destroy inside callbacks, disabled while editing, rapid input,
//   zero-size fields, a 1 MiB value, invalid UTF-8 and control characters, huge pastes.
// Callers: CTest (textinput fast, no GPU; paint is checked on a recording Painter).
#include <algorithm>
#include <string>

#include "FieldRig.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace {

using namespace r1ui::widgets;
namespace events = r1ui::core::events;
namespace layout = r1ui::core::layout;
using events::Key;
namespace Mod = events::Mod;

constexpr double kLeft = 10.0;   // root padding
constexpr double kTop = 10.0;
constexpr double kPad = 9.0;     // border 1 + padding 8 of the default tone

TextInput& makeInput(r1test::FieldRig& rig, double width = 200.0, TextInputTone tone = TextInputTone::Default) {
  TextInput& input = rig.ui.create<TextInput>(rig.ui.root(), tone, TextInputSize::Md);
  input.style().width = layout::Length::px(width);
  rig.layout();
  return input;
}

void testTypingAndCommit() {
  r1test::FieldRig rig;
  TextInput& input = makeInput(rig);
  std::string changed;
  std::string committed;
  int changes = 0, commits = 0;
  input.setOnTextChanged([&](std::string_view s) { changed = s; ++changes; });
  input.setOnCommitted([&](std::string_view s) { committed = s; ++commits; });
  rig.ui.router().focus(input.id(), events::FocusReason::Keyboard);
  rig.type("hello");
  R1_EXPECT(input.text() == "hello");
  R1_EXPECT(changes == 5 && changed == "hello" && commits == 0);
  R1_EXPECT(rig.key(Key::Enter));
  R1_EXPECT(commits == 1 && committed == "hello");
  rig.key(Key::Enter);  // Enter always commits, even unchanged
  R1_EXPECT(commits == 2);

  // Blur commits only when the text changed since the last commit.
  TextInput& other = makeInput(rig);
  rig.ui.router().focus(other.id(), events::FocusReason::Keyboard);
  R1_EXPECT(commits == 2);
  rig.ui.router().focus(input.id(), events::FocusReason::Keyboard);
  rig.key(Key::End);
  rig.type("!");
  rig.ui.router().focus(other.id(), events::FocusReason::Keyboard);
  R1_EXPECT(commits == 3 && committed == "hello!");
}

void testEscapeOrder() {
  r1test::FieldRig rig;
  TextInput& input = makeInput(rig);
  input.setText("start");
  int changes = 0;
  input.setOnTextChanged([&](std::string_view) { ++changes; });
  rig.ui.router().focus(input.id(), events::FocusReason::Keyboard);
  R1_EXPECT(input.selection().begin == 0 && input.selection().end == 5);  // Tab focus selects all
  R1_EXPECT(rig.key(Key::Escape));                                        // first: clears the selection
  R1_EXPECT(input.selection().empty() && input.text() == "start");
  R1_EXPECT(!rig.key(Key::Escape));                                       // unchanged text: unused, travels outward
  rig.type("XY");
  R1_EXPECT(input.text() == "startXY");
  const int before = changes;
  R1_EXPECT(rig.key(Key::Escape));  // reverts to the text at focus
  R1_EXPECT(input.text() == "start" && changes == before + 1);
  R1_EXPECT(!rig.key(Key::Escape));
}

void testKeyboardSelectionAndDeletion() {
  r1test::FieldRig rig;
  TextInput& input = makeInput(rig);
  rig.ui.router().focus(input.id(), events::FocusReason::Pointer);
  rig.type("one two three");
  rig.key(Key::Home);
  R1_EXPECT(input.caretOffset() == 0);
  rig.key(Key::Right, Mod::kCtrl);
  R1_EXPECT(input.caretOffset() == 4);  // start of "two"
  rig.key(Key::Right, Mod::kShift | Mod::kCtrl);
  R1_EXPECT(input.selection().begin == 4 && input.selection().end == 8);
  rig.key(Key::Delete);
  R1_EXPECT(input.text() == "one three");
  rig.key(Key::End);
  rig.key(Key::Backspace, Mod::kCtrl);
  R1_EXPECT(input.text() == "one ");
  R1_EXPECT(rig.ctrl('A'));
  R1_EXPECT(input.selection().begin == 0 && input.selection().end == input.text().size());
  rig.type("z");
  R1_EXPECT(input.text() == "z");
  // A plain letter never reaches shortcuts (rule 26) but Ctrl+S does (rule 27).
  R1_EXPECT(rig.key(static_cast<Key>('Q')));
  R1_EXPECT(!rig.ctrl('S'));
}

void testPointerSelection() {
  r1test::FieldRig rig;
  TextInput& input = makeInput(rig);
  input.setText("hello world again");
  const double left = kLeft + kPad;
  const double y = kTop + 13.0;
  rig.click(left + 3.0, y);
  R1_EXPECT(input.caretOffset() == 0 && input.focused());
  rig.click(left + 3.0, y, 2);  // double click: the word
  R1_EXPECT(input.selection().begin == 0 && input.selection().end == 5);
  rig.ui.pointerUp(left, y);
  rig.click(left + 3.0, y, 3);  // triple click: everything
  R1_EXPECT(input.selection().begin == 0 && input.selection().end == input.text().size());

  // Drag selects from the press to the pointer.
  rig.click(left + 1.0, y);
  rig.ui.pointerMove(left + 1.0, y);
  rig.ui.pointerDown(left + 1.0, y);
  rig.ui.pointerMove(left + 40.0, y);
  rig.ui.pointerUp(left + 40.0, y);
  R1_EXPECT(!input.selection().empty() && input.selection().begin == 0);
  // Shift-click extends from the caret.
  rig.click(left + 1.0, y);
  rig.click(left + 60.0, y, 1, Mod::kShift);
  R1_EXPECT(input.selection().begin == 0 && input.selection().end > 3);
  // A click right of the text puts the caret at the end.
  rig.click(left + 190.0, y);
  R1_EXPECT(input.caretOffset() == input.text().size());
}

void testClipboardAndUndo() {
  r1test::FieldRig rig;
  TextInput& input = makeInput(rig);
  rig.ui.router().focus(input.id(), events::FocusReason::Pointer);
  rig.type("copy me");
  rig.ctrl('A');
  R1_EXPECT(rig.ctrl('C'));
  R1_EXPECT(rig.clipboard == "copy me" && input.text() == "copy me");
  R1_EXPECT(rig.ctrl('X'));
  R1_EXPECT(rig.clipboard == "copy me" && input.text().empty());
  rig.ctrl('V');
  R1_EXPECT(input.text() == "copy me");
  rig.ctrl('V');
  R1_EXPECT(input.text() == "copy mecopy me");
  rig.ctrl('Z');
  R1_EXPECT(input.text() == "copy me");
  rig.ctrl('Y');
  R1_EXPECT(input.text() == "copy mecopy me");
}

void testClipboardFailures() {
  r1test::FieldRig rig;
  TextInput& input = makeInput(rig);
  rig.ui.router().focus(input.id(), events::FocusReason::Pointer);
  rig.type("keep");
  rig.ctrl('A');

  // A clipboard that throws: copy, cut and paste report nothing and leave the text and selection alone.
  rig.clipboardThrows = true;
  rig.ctrl('C');
  rig.ctrl('X');
  R1_EXPECT(input.text() == "keep" && input.selection().end == 4);
  rig.ctrl('V');
  R1_EXPECT(input.text() == "keep");
  rig.clipboardThrows = false;

  // An empty clipboard: paste does nothing.
  rig.clipboardEmpty = true;
  rig.ctrl('V');
  R1_EXPECT(input.text() == "keep");
  rig.clipboardEmpty = false;

  // Hostile clipboard text: control characters and invalid UTF-8 are repaired, newlines dropped.
  rig.clipboard = std::string("a\nb\tc\x01") + "\xC3\x28" + "\xF0\x9F\x98\x80";
  rig.ctrl('V');
  R1_EXPECT(input.text().find('\n') == std::string::npos && input.text().find('\t') == std::string::npos);
  R1_EXPECT(input.text().find("\xEF\xBF\xBD") != std::string::npos);        // U+FFFD for the broken byte
  R1_EXPECT(input.text().find("\xF0\x9F\x98\x80") != std::string::npos);    // the emoji survived

  // A multi-megabyte paste is cut at the editor's byte cap and never grows the text past it.
  rig.clipboard.assign(5u * 1024u * 1024u, 'x');
  rig.ctrl('A');
  rig.ctrl('V');
  R1_EXPECT(input.text().size() <= 1024u * 1024u);
  R1_EXPECT(rig.paint() < 4000);  // the huge value is drawn as a window, not as a million glyph quads
}

void testMaxLengthCountsGraphemes() {
  r1test::FieldRig rig;
  TextInput& input = makeInput(rig);
  input.setMaxLength(3);
  rig.ui.router().focus(input.id(), events::FocusReason::Pointer);
  rig.type("abcdef");
  R1_EXPECT(input.text() == "abc");
  rig.ctrl('A');
  rig.clipboard = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7xyz";  // family emoji (one grapheme) then xyz
  rig.ctrl('V');
  R1_EXPECT(input.text() == std::string("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7xy"));  // 3 graphemes
  rig.type("q");
  R1_EXPECT(input.text().back() == 'y');  // full: typing is ignored
  // setText honours the limit too, and shorter text can grow back.
  input.setText("123456");
  R1_EXPECT(input.text() == "123");
  rig.key(Key::Backspace);
  rig.type("9");
  R1_EXPECT(input.text() == "129");
  input.setMaxLength(0);
  rig.type("0000");
  R1_EXPECT(input.text() == "1290000");
}

void testReadOnlyAndDisabled() {
  r1test::FieldRig rig;
  TextInput& input = makeInput(rig);
  input.setText("fixed");
  input.setReadOnly(true);
  int changes = 0;
  input.setOnTextChanged([&](std::string_view) { ++changes; });
  rig.ui.router().focus(input.id(), events::FocusReason::Keyboard);
  rig.type("zzz");
  rig.key(Key::Backspace);
  rig.ctrl('V');
  rig.ctrl('X');
  R1_EXPECT(input.text() == "fixed" && changes == 0);
  rig.ctrl('C');  // copy keeps working
  R1_EXPECT(rig.clipboard == "fixed");
  rig.key(Key::Left);
  R1_EXPECT(input.caretOffset() < 5);  // the caret moves
  input.setText("changed by the owner");
  R1_EXPECT(input.text() == "changed by the owner");  // setText is the owner's path

  // Disabling a field that holds an uncommitted edit commits it once and ignores further input.
  input.setReadOnly(false);
  int commits = 0;
  std::string last;
  input.setOnCommitted([&](std::string_view s) { ++commits; last = s; });
  rig.key(Key::End);
  rig.type("!");
  R1_EXPECT(input.text().back() == '!');
  input.setEnabled(false);
  R1_EXPECT(commits == 1 && last == "changed by the owner!");
  rig.type("nope");
  rig.click(kLeft + 30, kTop + 13);
  R1_EXPECT(input.text() == "changed by the owner!" && commits == 1);
  R1_EXPECT(rig.paint() > 0);
}

void testDestroyInsideCallbacks() {
  {
    r1test::FieldRig rig;
    TextInput& input = makeInput(rig);
    const auto id = input.id();
    input.setOnTextChanged([&](std::string_view) { rig.ui.destroy(id); });
    rig.ui.router().focus(id, events::FocusReason::Pointer);
    rig.type("abc");  // the first character destroys the widget; the rest go nowhere
    R1_EXPECT(!rig.ui.alive(id));
    rig.key(Key::Enter);
    (void)rig.paint();
  }
  {
    r1test::FieldRig rig;
    TextInput& input = makeInput(rig);
    const auto id = input.id();
    input.setOnCommitted([&](std::string_view) { rig.ui.destroy(id); });
    rig.ui.router().focus(id, events::FocusReason::Pointer);
    rig.type("x");
    rig.key(Key::Enter);
    R1_EXPECT(!rig.ui.alive(id));
    (void)rig.paint();
  }
  {
    // Blur commit that destroys the field.
    r1test::FieldRig rig;
    TextInput& input = makeInput(rig);
    TextInput& other = makeInput(rig);
    const auto id = input.id();
    input.setOnCommitted([&](std::string_view) { rig.ui.destroy(id); });
    rig.ui.router().focus(id, events::FocusReason::Pointer);
    rig.type("x");
    rig.ui.router().focus(other.id(), events::FocusReason::Pointer);
    R1_EXPECT(!rig.ui.alive(id));
  }
}

void testRapidAndHostileInput() {
  r1test::FieldRig rig;
  TextInput& input = makeInput(rig);
  rig.ui.router().focus(input.id(), events::FocusReason::Pointer);
  for (int i = 0; i < 20000; ++i) rig.ui.textInput(U'a' + static_cast<char32_t>(i % 26));
  R1_EXPECT(input.text().size() == 20000);
  R1_EXPECT(rig.paint() < 4000);  // a window around the caret, not 20000 glyphs
  // Invalid code points and controls through the platform path never reach the text.
  rig.ui.textInput(0xD800);
  rig.ui.textInput(0x110000);
  rig.ui.textInput(0);
  rig.ui.textInput(U'\n');
  rig.ui.textInput(U'\t');
  rig.ui.textInput(0x7F);
  R1_EXPECT(input.text().size() == 20000);
  // Rapid Home / End / Backspace / undo storms keep every invariant.
  for (int i = 0; i < 300; ++i) {
    rig.key(i % 2 == 0 ? Key::Home : Key::End);
    rig.key(Key::Backspace, i % 5 == 0 ? Mod::kCtrl : 0);
    if (i % 7 == 0) rig.ctrl('Z');
  }
  R1_EXPECT(input.caretOffset() <= input.text().size());
  (void)rig.paint();

  // setText with invalid UTF-8 and control characters.
  input.setText(std::string("ok\xFF\xFE") + "\x01" + "end\r\n");
  R1_EXPECT(input.text() == "ok\xEF\xBF\xBD\xEF\xBF\xBD" "end");
  // A 3 MiB value is cut to the cap and still paints and edits.
  input.setText(std::string(3u * 1024u * 1024u, 'w'));
  R1_EXPECT(input.text().size() <= 1024u * 1024u && !input.text().empty());
  R1_EXPECT(rig.paint() < 4000);
  rig.type("Z");  // the value sits at the byte cap: nothing more is accepted
  R1_EXPECT(input.text()[0] == 'w');
  rig.key(Key::Home);
  rig.key(Key::Delete);
  rig.type("Z");
  R1_EXPECT(input.text()[0] == 'Z');
}

void testZeroAndTinyWidth() {
  for (const double width : {0.0, 1.0, 2.0, 19.0, 30.0}) {
    r1test::FieldRig rig;
    TextInput& input = makeInput(rig, width);
    input.setText("a long value that cannot fit");
    input.setPlaceholder("placeholder");
    input.setClearable(true);
    rig.ui.router().focus(input.id(), events::FocusReason::Keyboard);
    rig.type("more");
    rig.key(Key::End);
    rig.click(kLeft + 2.0, kTop + 13.0);
    rig.click(kLeft + 2.0, kTop + 13.0, 2);
    (void)rig.paint();
    R1_EXPECT(input.text().size() >= 4);
  }
  for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f, 3.0f}) {
    r1test::FieldRig rig(400, 300, scale);
    TextInput& input = makeInput(rig, 150.0, TextInputTone::Panel);
    input.setText("scaled text");
    rig.ui.router().focus(input.id(), events::FocusReason::Keyboard);
    R1_EXPECT(rig.paint() > 0);
  }
}

void testScrollKeepsCaretVisible() {
  r1test::FieldRig rig;
  TextInput& input = makeInput(rig, 120.0);
  rig.ui.router().focus(input.id(), events::FocusReason::Pointer);
  rig.type("this text is much wider than the field it is typed into");
  const layout::Rect box = rig.ui.absRect(input.id());
  layout::Rect caret = input.caretRect();
  R1_EXPECT(!(caret.w == 0 && caret.h == 0));
  R1_EXPECT(caret.x >= box.x && caret.x <= box.right());
  rig.key(Key::Home);
  caret = input.caretRect();
  R1_EXPECT(caret.x >= box.x && caret.x <= box.x + 12);
  rig.key(Key::End);
  caret = input.caretRect();
  R1_EXPECT(caret.x <= box.right());
  // Clicking near the right edge of the scrolled text places the caret near the end, not at 0.
  rig.click(box.right() - 12.0, kTop + 13.0);
  R1_EXPECT(input.caretOffset() > 30);
}

void testStatesAndPaint() {
  r1test::FieldRig rig;
  for (const TextInputTone tone : {TextInputTone::Default, TextInputTone::Panel}) {
    for (const TextInputSize size : {TextInputSize::Sm, TextInputSize::Md}) {
      TextInput& input = rig.ui.create<TextInput>(rig.ui.root(), tone, size);
      input.style().width = layout::Length::px(180);
      input.setPlaceholder("placeholder");
      rig.layout();
      R1_EXPECT(rig.ui.absRect(input.id()).h == 26);
      R1_EXPECT(rig.paint() > 0);  // placeholder
      input.setText("value");
      input.setInvalid(true);
      input.setBound(true);
      R1_EXPECT(rig.paint() > 0);
      input.setBound(false);
      input.setInvalid(false);
      input.setText("");
      input.setMixed(true);
      R1_EXPECT(rig.paint() > 0);  // "Mixed"
      rig.ui.router().focus(input.id(), events::FocusReason::Pointer);
      rig.type("typed");
      R1_EXPECT(!input.hasState(StateFlag::kMixed) && input.text() == "typed");  // editing clears mixed
      input.setEnabled(false);
      R1_EXPECT(rig.paint() > 0);
    }
  }
  R1_EXPECT(std::string(TextInput::styleKeyFor(TextInputTone::Default, TextInputSize::Md)) == "input.default");
  R1_EXPECT(std::string(TextInput::styleKeyFor(TextInputTone::Panel, TextInputSize::Sm)) == "input.panel.sm");
  TextInput& named = rig.ui.create<TextInput>(rig.ui.root());
  named.setPlaceholder("Search");
  R1_EXPECT(named.accessibleName() == "Search");
  named.setAccessibleName("Find");
  R1_EXPECT(named.accessibleName() == "Find");
  R1_EXPECT(named.cursor() == Cursor::Text);
}

void testClearButton() {
  r1test::FieldRig rig;
  TextInput& input = makeInput(rig, 200.0);
  input.setClearable(true);
  int changes = 0;
  input.setOnTextChanged([&](std::string_view) { ++changes; });
  input.setText("abc");
  const layout::Rect box = rig.ui.absRect(input.id());
  const double x = box.right() - 17.0;  // centre of the 16 px clear box
  const double y = box.y + box.h / 2.0;
  rig.ui.pointerMove(x, y);
  R1_EXPECT(input.cursor() == Cursor::Pointer);
  rig.click(x, y);
  R1_EXPECT(input.text().empty() && changes == 1);
  rig.click(x, y);  // nothing to clear: no glyph, the click lands in the text area
  R1_EXPECT(changes == 1);
  input.setText("xyz");
  input.setReadOnly(true);
  rig.click(x, y);
  R1_EXPECT(input.text() == "xyz");
}

void testImeHooks() {
  r1test::FieldRig rig;
  TextInput& input = makeInput(rig);
  int changes = 0;
  input.setOnTextChanged([&](std::string_view) { ++changes; });
  rig.ui.router().focus(input.id(), events::FocusReason::Pointer);
  rig.type("ab");
  R1_EXPECT(input.setPreedit("\xE3\x81\x82", 3));  // a Japanese preedit character
  R1_EXPECT(input.composing() && input.text() == "ab" && changes == 2);
  R1_EXPECT(rig.paint() > 0);
  R1_EXPECT(input.caretRect().h > 0);
  R1_EXPECT(input.commitPreedit());
  R1_EXPECT(!input.composing() && input.text() == "ab\xE3\x81\x82" && changes == 3);
  R1_EXPECT(input.setPreedit("zz", 2));
  R1_EXPECT(rig.key(Key::Escape));  // Escape ends the composition first
  R1_EXPECT(!input.composing() && input.text() == "ab\xE3\x81\x82");
  input.setReadOnly(true);
  R1_EXPECT(!input.setPreedit("x", 1));
}

void testTabNavigationAndFocusRules() {
  r1test::FieldRig rig;
  TextInput& a = makeInput(rig);
  TextInput& b = makeInput(rig);
  a.setText("first");
  b.setText("second");
  rig.tab();
  R1_EXPECT(a.focused() && a.focusVisible());
  R1_EXPECT(a.selection().begin == 0 && a.selection().end == 5);  // keyboard focus selects all
  rig.tab();
  R1_EXPECT(b.focused() && !a.focused());
  rig.ui.keyDown(Key::Tab, Mod::kShift);
  R1_EXPECT(a.focused());
  // Focus by pointer puts the caret where it was pressed and shows no focus indication.
  rig.click(kLeft + 120.0, kTop + 13.0 + 34.0);  // inside b, right of its text
  R1_EXPECT(b.focused() && !b.focusVisible() && b.caretOffset() == b.text().size());
  // Tab inside a focused field is not text.
  rig.type("\t");
  R1_EXPECT(b.text() == "second");
}

void testLineEditorPolicy() {
  R1_EXPECT(LineEditor::sanitizeLine(std::string("a\x01" "b\x7F" "c\xC2\x85" "d\xE2\x80\xA9" "e")) == "abcde");  // C0, DEL, C1 (U+0085) and U+2029 dropped
  R1_EXPECT(LineEditor::sanitizeLine("\xFF") == "\xEF\xBF\xBD");
  R1_EXPECT(LineEditor::sanitizeLine("").empty());
  R1_EXPECT(LineEditor::graphemeCount("") == 0 && LineEditor::graphemeCount("abc") == 3);
  R1_EXPECT(LineEditor::graphemeCount("e\xCC\x81") == 1);                                            // e + combining acute
  R1_EXPECT(LineEditor::graphemeCount("\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA") == 1);                       // a flag (two regional indicators)
  R1_EXPECT(LineEditor::graphemeCount("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9") == 1);          // ZWJ sequence
}

void testBlinkKeepsFramesComing() {
  // Foundation regression: a colour transition that ends must not cancel the caret's frame request.
  r1test::FieldRig rig;
  TextInput& input = makeInput(rig);
  rig.ui.setAnimationsEnabled(true);
  rig.ui.setFrameLoopRunning(true);
  rig.ui.setTime(900);
  (void)rig.paint();  // the first paint creates the colour tweens at rest
  rig.ui.setTime(1000);
  rig.ui.pointerMove(kLeft + 20.0, kTop + 13.0);  // hover and focus start 150 ms transitions
  rig.ui.router().focus(input.id(), events::FocusReason::Keyboard);
  (void)rig.paint();
  R1_EXPECT(rig.ui.needsFrame());
  rig.ui.setTime(1100);
  (void)rig.paint();
  rig.ui.setTime(1400);  // the transitions are over
  (void)rig.paint();
  rig.layout();
  R1_EXPECT(rig.ui.needsFrame());  // still blinking
  // Leaving the field stops the frames.
  rig.ui.router().clearFocus();
  rig.ui.setTime(2000);
  (void)rig.paint();
  rig.ui.setTime(2300);  // the focus transition back is over
  (void)rig.paint();
  (void)rig.paint();
  rig.layout();
  R1_EXPECT(!rig.ui.needsFrame());
}

}  // namespace

int main() {
  testTypingAndCommit();
  testEscapeOrder();
  testKeyboardSelectionAndDeletion();
  testPointerSelection();
  testClipboardAndUndo();
  testClipboardFailures();
  testMaxLengthCountsGraphemes();
  testReadOnlyAndDisabled();
  testDestroyInsideCallbacks();
  testRapidAndHostileInput();
  testZeroAndTinyWidth();
  testScrollKeepsCaretVisible();
  testStatesAndPaint();
  testClearButton();
  testImeHooks();
  testTabNavigationAndFocusRules();
  testLineEditorPolicy();
  testBlinkKeepsFramesComing();
  return r1test::finish();
}
