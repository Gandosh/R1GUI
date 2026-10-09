// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for PickerEntry (the private text entry of the pickers) and the number text
//   helpers: commit / revert protocol (Enter, Escape, focus loss), programmatic setText while the user
//   is editing, rejected commits, stepping, clipboard, selection by pointer, hostile text (invalid
//   UTF-8, control characters, huge paste, NaN-like numbers) and destroy-inside-callback.
// Why: every numeric field of the pickers and the curve editor relies on these rules.
// Callers: CTest (colorpicker, fast tier, no GPU).
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/colorpicker/NumberText.h"
#include "r1ui/widgets/colorpicker/PickerEntry.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Key;
// Letter keys are not named in the Key enum (A..Z are 65..90).
constexpr Key kKeyC = static_cast<Key>('C');
constexpr Key kKeyV = static_cast<Key>('V');
constexpr Key kKeyX = static_cast<Key>('X');
constexpr Key kKeyZ = static_cast<Key>('Z');
namespace events = r1ui::core::events;
namespace layout = r1ui::core::layout;

void type(UiContext& ui, std::string_view ascii) {
  for (char c : ascii) ui.textInput(static_cast<char32_t>(c));
}

void paintOnce(r1test::TestUi& t) {
  r1ui::render::Painter painter;
  painter.begin(static_cast<uint32_t>(t.ui.viewportWidth()), static_cast<uint32_t>(t.ui.viewportHeight()));
  t.ui.paint(painter);
  t.ui.finishPaint();
  painter.end();
}

struct Fixture {
  r1test::TestUi t{400, 200};
  PickerEntry* entry = nullptr;
  std::vector<std::string> commits;
  bool accept = true;
  Fixture() {
    entry = &t.ui.create<PickerEntry>(t.ui.root(), PickerEntry::Look::Field);
    entry->style().width = layout::Length::px(120);
    t.ui.rootStyle().alignItems = layout::Align::Start;
    entry->setText("100");
    entry->onCommit = [this](std::string_view s) {
      commits.emplace_back(s);
      return accept;
    };
    t.layout();
  }
  void focusByKeyboard() { t.ui.router().focus(entry->id(), events::FocusReason::Keyboard); }
};

void testParseAndFormat() {
  R1_EXPECT(parseNumber("12").value() == 12 && parseNumber(" -3.5 ").value() == -3.5 && parseNumber("+7").value() == 7);
  R1_EXPECT(parseNumber(".5").value() == 0.5 && parseNumber("5.").value() == 5 && parseNumber("1e3").value() == 1000 && parseNumber("-2.5E-1").value() == -0.25);
  const char* bad[] = {"", " ", "abc", "nan", "inf", "-inf", "0x10", "1,5", "1 2", "--1", "1e", "e5", ".", "+", "-", "1.2.3", "\xE2\x80\x93" "5", "1e999", "١٢"};
  for (const char* b : bad) R1_EXPECT(!parseNumber(b).has_value());
  R1_EXPECT(!parseNumber(std::string(65, '1')) && !parseNumber(std::string(100000, '9')));
  R1_EXPECT(parseNumber(std::string(60, '1')).has_value());
  R1_EXPECT(!parseNumber(std::string("1\0" "2", 3)));
  R1_EXPECT(formatNumber(1.0, 3) == "1" && formatNumber(1.5, 3) == "1.5" && formatNumber(0.1 + 0.2, 6) == "0.3" && formatNumber(-0.0, 2) == "0");
  R1_EXPECT(formatNumber(-0.0004, 3) == "0" && formatNumber(123456.789012345, 6) == "123456.789012" && formatNumber(2.0 / 3.0, 4) == "0.6667");
  R1_EXPECT(formatNumber(std::numeric_limits<double>::quiet_NaN(), 3) == "0" && formatNumber(std::numeric_limits<double>::infinity(), 3) == "0");
  R1_EXPECT(formatNumber(1e300, 2).size() > 100 && formatNumber(5, -4) == "5" && formatNumber(0.123456789, 99) == "0.123456789");
  // Round trip.
  for (double v : {0.0, 1.0, -1.5, 3.14159, 1e-6, 123456.0}) R1_EXPECT(std::fabs(parseNumber(formatNumber(v, 6)).value() - v) < 1e-6);
}

void testCommitAndRevert() {
  Fixture f;
  f.focusByKeyboard();
  R1_EXPECT(f.entry->focused() && f.entry->editor().hasSelection());  // keyboard focus selects all
  type(f.t.ui, "42");
  R1_EXPECT(f.entry->text() == "42" && f.entry->dirty());
  f.t.ui.keyDown(Key::Enter);
  R1_EXPECT(f.commits.size() == 1 && f.commits[0] == "42" && !f.entry->dirty() && f.entry->text() == "42");
  f.t.ui.keyDown(Key::Enter);  // nothing changed: no second commit
  R1_EXPECT(f.commits.size() == 1);

  // Escape reverts a dirty entry, then is left to the popup.
  f.t.ui.keyDown(Key::Backspace);
  R1_EXPECT(f.entry->dirty());
  R1_EXPECT(f.t.ui.keyDown(Key::Escape) && f.entry->text() == "42" && !f.entry->dirty());
  R1_EXPECT(!f.t.ui.keyDown(Key::Escape));  // clean: not consumed

  // A rejected commit reverts to the last accepted text.
  f.accept = false;
  f.t.ui.keyDown(Key::A, events::Mod::kCtrl);
  type(f.t.ui, "zz");
  f.t.ui.keyDown(Key::Enter);
  R1_EXPECT(f.commits.back() == "zz" && f.entry->text() == "42" && !f.entry->dirty());

  // Losing focus commits a dirty entry.
  f.accept = true;
  f.t.ui.keyDown(Key::A, events::Mod::kCtrl);
  type(f.t.ui, "7");
  f.t.ui.router().clearFocus();
  R1_EXPECT(f.commits.back() == "7" && f.entry->text() == "7");
}

void testProgrammaticSetText() {
  Fixture f;
  f.focusByKeyboard();
  type(f.t.ui, "5");  // dirty
  f.entry->setText("999");
  R1_EXPECT(f.entry->text() == "5");  // the user's text is not clobbered
  f.t.ui.keyDown(Key::Escape);        // revert applies the newest programmatic value
  R1_EXPECT(f.entry->text() == "999" && !f.entry->dirty());
  f.entry->setText("1");
  R1_EXPECT(f.entry->text() == "1");  // not dirty: applied at once
  // The owner's own refresh during a commit shows the canonical text.
  f.entry->onCommit = [&](std::string_view s) {
    f.entry->setText(std::string("<") + std::string(s) + ">");
    return true;
  };
  f.t.ui.keyDown(Key::A, events::Mod::kCtrl);
  type(f.t.ui, "8");
  f.t.ui.keyDown(Key::Enter);
  R1_EXPECT(f.entry->text() == "<8>");
}

void testStepAndKeys() {
  Fixture f;
  int total = 0;
  f.entry->onStep = [&](int s) { total += s; };
  f.focusByKeyboard();
  f.t.ui.keyDown(Key::Up);
  f.t.ui.keyDown(Key::Down, events::Mod::kShift);
  R1_EXPECT(total == 1 - 10);
  f.entry->onStep = nullptr;
  R1_EXPECT(!f.t.ui.keyDown(Key::Up));
  // Caret keys and selection.
  f.t.ui.keyDown(Key::Home);
  f.t.ui.keyDown(Key::Right, events::Mod::kShift);
  R1_EXPECT(f.entry->editor().hasSelection());
  f.t.ui.keyDown(Key::Delete);
  R1_EXPECT(f.entry->text() == "00" && f.entry->dirty());
  f.t.ui.keyDown(kKeyZ, events::Mod::kCtrl);
  R1_EXPECT(f.entry->text() == "100");
  f.t.ui.keyDown(Key::Tab);  // not consumed by the entry (focus traversal)
}

void testClipboardAndPointer() {
  // A context with clipboard hooks: copy writes the selection, paste reads (and sanitises) text.
  r1test::TestUi t(400, 200);
  std::string clipboard = "  12.5";
  UiContextOptions options;
  options.host.writeClipboard = [&](std::string_view s) { clipboard.assign(s); };
  options.host.readClipboard = [&]() -> std::optional<std::string> { return clipboard; };
  UiContext ui(t.services, options);
  ui.setViewport(400, 200, 1.0f);
  ui.setAnimationsEnabled(false);
  ui.rootStyle().alignItems = layout::Align::Start;
  PickerEntry& e = ui.create<PickerEntry>(ui.root(), PickerEntry::Look::Field);
  e.style().width = layout::Length::px(120);
  e.setText("100");
  ui.frame();
  ui.router().focus(e.id(), events::FocusReason::Keyboard);
  ui.keyDown(kKeyC, events::Mod::kCtrl);
  R1_EXPECT(clipboard == "100");
  ui.keyDown(kKeyV, events::Mod::kCtrl);  // replaces the selection with the (sanitised) clipboard
  R1_EXPECT(e.text() == "100" && e.dirty() == false);
  clipboard = "7	8";
  ui.keyDown(Key::A, events::Mod::kCtrl);
  ui.keyDown(kKeyV, events::Mod::kCtrl);
  R1_EXPECT(e.text().find('	') == std::string::npos && e.dirty());
  ui.keyDown(Key::A, events::Mod::kCtrl);
  ui.keyDown(kKeyX, events::Mod::kCtrl);
  R1_EXPECT(e.text().empty() && clipboard.find('7') != std::string::npos);

  // Press places the caret; a drag selects; the press focuses.
  Fixture f;
  f.t.ui.router().clearFocus();
  const layout::Rect r = f.t.ui.absRect(f.entry->id());
  f.t.ui.pointerMove(r.x + 100, r.y + 10);
  f.t.ui.pointerDown(r.x + 100, r.y + 10);
  R1_EXPECT(f.entry->focused());
  R1_EXPECT(f.entry->editor().caret() == 3 && !f.entry->editor().hasSelection());
  f.t.ui.pointerMove(r.x + 1, r.y + 10);
  R1_EXPECT(f.entry->editor().hasSelection());
  f.t.ui.pointerUp(r.x + 1, r.y + 10);
  f.t.ui.pointerDown(r.x + 3, r.y + 10);
  f.t.ui.pointerUp(r.x + 3, r.y + 10);
  paintOnce(f.t);  // caret and selection painting never throw
}

void testHostile() {
  Fixture f;
  f.focusByKeyboard();
  f.entry->setText("a\xFF\xFE" "b\tc\n");  // invalid UTF-8 and control characters are sanitised by the editor
  paintOnce(f.t);
  R1_EXPECT(f.entry->text().find('\t') == std::string::npos && f.entry->text().find('\n') == std::string::npos);
  f.entry->setText(std::string(5'000'000, 'x'));  // above the editor limits: cut, never throws
  paintOnce(f.t);
  R1_EXPECT(f.entry->text().size() <= r1ui::text::kDefaultMaxBytes);
  f.entry->setMaxBytes(8);
  f.entry->setText("123456789012");
  R1_EXPECT(f.entry->text().size() <= 12);
  f.t.ui.textInput(0x1F600);       // emoji
  f.t.ui.textInput(0xD800);        // lone surrogate
  f.t.ui.textInput(0x110000);      // out of range
  f.t.ui.textInput(0);
  paintOnce(f.t);
  f.entry->setFontSize(std::numeric_limits<double>::quiet_NaN());
  f.entry->setPadLeft(std::numeric_limits<double>::infinity());
  paintOnce(f.t);
  f.entry->style().width = layout::Length::px(0);
  f.entry->requestLayout();
  f.t.layout();
  paintOnce(f.t);
  f.entry->style().width = layout::Length::px(1);
  f.entry->requestLayout();
  f.t.layout();
  paintOnce(f.t);
}

void testDestroyInsideCommit() {
  Fixture f;
  f.entry->onCommit = [&](std::string_view) {
    f.t.ui.destroy(f.entry->id());
    return true;
  };
  f.focusByKeyboard();
  type(f.t.ui, "9");
  f.t.ui.keyDown(Key::Enter);  // destroys the entry from its own handler
  f.t.layout();
  paintOnce(f.t);
  R1_EXPECT(true);
}

}  // namespace

int main() {
  testParseAndFormat();
  testCommitAndRevert();
  testProgrammaticSetText();
  testStepAndKeys();
  testClipboardAndPointer();
  testHostile();
  testDestroyInsideCommit();
  return r1test::finish();
}
