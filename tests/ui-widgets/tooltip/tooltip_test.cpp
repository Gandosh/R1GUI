// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: behaviour tests of the tooltip visual and its registry without a GPU: the 410 ms delay, the
//   measured box (26 px high for one line, text + 18 wide), rich content (shortcut after the title,
//   wrapped description), plain tooltips going through the same box, the chord and shortcut text
//   helpers of spec 07, text wrapping with hostile input (invalid UTF-8, one huge word, a megabyte of
//   text, non-finite widths) and the foundation fixes this widget group relies on (a visible tooltip
//   must not shield an open menu from Escape or an outside press, timers).
// Callers: CTest (label fast).
#include "TestSupport.h"
#include "r1ui/widgets/menu/MenuController.h"
#include "r1ui/widgets/tooltip/TooltipContent.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Key;
namespace Mod = r1ui::core::events::Mod;
constexpr Key letter(char c) { return static_cast<Key>(c); }
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;

struct Box : WidgetObject {
  const char* typeName() const override { return "Box"; }
  void onAttached() override {
    style().width = layout::Length::px(100);
    style().height = layout::Length::px(30);
    style().position = layout::Position::Absolute;
    style().inset[layout::kLeft] = layout::Length::px(50);
    style().inset[layout::kTop] = layout::Length::px(50);
  }
};

// Moves the pointer over `box` at `startMs` and ticks the clock to `untilMs` in 5 ms steps; returns
// the time at which the tooltip became visible (0 = never).
uint64_t timeToAppear(r1test::TestUi& t, uint64_t startMs, uint64_t untilMs) {
  t.ui.setTime(startMs);
  t.ui.pointerMove(60, 60);
  for (uint64_t now = startMs; now <= untilMs; now += 5) {
    t.ui.setTime(now);
    t.ui.tick();
    t.layout();
    if (t.ui.tooltips().visible()) return now - startMs;
  }
  return 0;
}

void testDelayAndBox() {
  r1test::TestUi t(400, 300);
  const auto registry = RichTooltips::install(t.ui);
  R1_EXPECT(t.ui.tooltips().timing().restMs + t.ui.tooltips().timing().showDelayMs == 410);
  Box& box = t.ui.create<Box>(t.ui.root());
  box.setTooltip("Flip horizontal");
  t.layout();
  const uint64_t delay = timeToAppear(t, 1000, 1600);
  R1_EXPECT(delay >= 410 && delay <= 415);  // 410 ms, in 5 ms steps
  // A plain tooltip gets the measured box: one 16 px line + 4 + 4 padding + 2 border = 26.
  const auto host = t.ui.absRect(t.ui.tooltips().overlay().host);
  R1_EXPECT(host.h == 26);
  const double textWidth = t.ui.text().measure("Flip horizontal", 12.0f) / 1.0;
  R1_EXPECT_NEAR(static_cast<double>(host.w), textWidth + 18.0, 1.0);
}

void testRichContent() {
  r1test::TestUi t(400, 300);
  const auto registry = RichTooltips::install(t.ui);
  Box& box = t.ui.create<Box>(t.ui.root());
  TooltipInfo info;
  info.title = "Undo";
  info.shortcut = "Ctrl+Z";
  info.description = "Reverts the last change to the document and keeps the redo history so it can be restored later";
  R1_EXPECT(registry->set(box.id(), info));
  R1_EXPECT(registry->find(box.id()) != nullptr && std::string(box.tooltipText()) == "Undo");
  t.layout();
  R1_EXPECT(timeToAppear(t, 1000, 1600) != 0);
  const auto host = t.ui.absRect(t.ui.tooltips().overlay().host);
  // Title line, shortcut on the same line, the description wraps to several muted lines.
  R1_EXPECT(host.h > 26 + 14);
  R1_EXPECT(host.w <= 280 + 18);
  // A stale widget and an empty title are refused.
  R1_EXPECT(!registry->set(WidgetId{}, info));
  TooltipInfo empty;
  R1_EXPECT(!registry->set(box.id(), empty));
  registry->clear(box.id());
  R1_EXPECT(registry->find(box.id()) == nullptr && box.tooltipText().empty());
  // The registry stays bounded when widgets come and go.
  for (int i = 0; i < 600; ++i) {
    Box& b = t.ui.create<Box>(t.ui.root());
    TooltipInfo one;
    one.title = "x";
    registry->set(b.id(), one);
    t.ui.destroy(b.id());
  }
  R1_EXPECT(registry->size() < 600);
}

void testWrapping() {
  r1test::TestUi t(400, 300);
  UiContext& ui = t.ui;
  const auto lines = [&](std::string_view text, double width, size_t max = 12) { return wrapTooltipText(ui, text, 12.0, 400, width, max); };
  R1_EXPECT(lines("", 100).empty() || lines("", 100)[0].empty());
  R1_EXPECT(lines("short", 200).size() == 1);
  const auto wrapped = lines("one two three four five six seven eight nine ten", 80);
  R1_EXPECT(wrapped.size() > 3);
  for (const std::string& l : wrapped) R1_EXPECT(ui.text().measure(l, 12.0f) <= 80.5f);
  // Hard breaks, runs of spaces and a word wider than the line.
  R1_EXPECT(lines("a\nb\nc", 200).size() == 3);
  R1_EXPECT(lines("a     b", 200).size() == 1);
  const auto longWord = lines("Supercalifragilisticexpialidocious", 60);
  R1_EXPECT(longWord.size() > 1);
  std::string joined;
  for (const std::string& l : longWord) joined += l;
  R1_EXPECT(joined == "Supercalifragilisticexpialidocious");
  // Line limit: the last line ends with an ellipsis.
  const auto capped = lines("a b c d e f g h i j k l m n o p q r s t", 8, 3);
  R1_EXPECT(capped.size() == 3 && capped.back().size() >= 3 && capped.back().substr(capped.back().size() - 3) == "\xE2\x80\xA6");
  // Hostile input: invalid UTF-8, a megabyte, non-finite and tiny widths, zero lines.
  R1_EXPECT(!lines(std::string("ab\xFF\xFE\xC0\xAF" "cd"), 100).empty());
  const std::string huge(1 << 20, 'w');
  const auto cut = lines(huge, 120, 12);
  R1_EXPECT(cut.size() <= 12);
  R1_EXPECT(!lines("text", std::nan("")).empty());
  R1_EXPECT(!lines("text", 0.0).empty());
  R1_EXPECT(lines("text", 100, 0).empty());
  R1_EXPECT(!lines("\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xF0\x9F\x98\x80", 5).empty());
}

void testChordText() {
  R1_EXPECT(formatChordText(letter('D'), Mod::kCtrl) == "Ctrl+D");
  R1_EXPECT(formatChordText(letter('Z'), Mod::kCtrl | Mod::kShift) == "Ctrl+Shift+Z");
  R1_EXPECT(formatChordText(letter('S'), Mod::kShift | Mod::kAlt | Mod::kCtrl | Mod::kMeta) == "Ctrl+Cmd+Alt+Shift+S");  // spec 07 order
  R1_EXPECT(formatChordText(Key::F1, 0) == "F1" && formatChordText(Key::PageUp, 0) == "Page Up");
  R1_EXPECT(formatChordText(letter('D'), Mod::kCtrl, true) == "CTRL+D");
  R1_EXPECT(formatChordText(Key::Unknown, Mod::kCtrl).empty());
  R1_EXPECT(formatChordText(static_cast<Key>(9999), 0).empty());
  R1_EXPECT(tooltipWithShortcut("Duplicate", "Ctrl+D") == "Duplicate (Ctrl+D)");
  R1_EXPECT(tooltipWithShortcut("Duplicate", "") == "Duplicate");
  R1_EXPECT(tooltipWithShortcut("", "Ctrl+D") == "(Ctrl+D)");
}

// A visible tooltip is an overlay above the menu: it must neither shield the menu from an outside
// press nor swallow Escape (foundation fix in OverlayManager).
void testTooltipDoesNotShieldMenu() {
  for (const bool escape : {true, false}) {
    r1test::TestUi t(400, 300);
    RichTooltips::install(t.ui);
    Box& box = t.ui.create<Box>(t.ui.root());
    box.setTooltip("Hint");
    t.layout();
    MenuController c(t.ui);
    MenuSpec spec;
    spec.items = {menuAction("a", "A"), menuAction("b", "B")};
    R1_EXPECT(c.openContextMenu(spec, 250, 150));
    t.layout();
    R1_EXPECT(timeToAppear(t, 2000, 2600) != 0);
    R1_EXPECT(t.ui.overlays().count() == 2);
    if (escape) {
      t.ui.keyDown(Key::Escape);
    } else {
      t.ui.pointerDown(380, 280);
      t.ui.pointerUp(380, 280);
    }
    R1_EXPECT(!c.isOpen());
  }
}

}  // namespace

int main() {
  testDelayAndBox();
  testRichContent();
  testWrapping();
  testChordText();
  testTooltipDoesNotShieldMenu();
  return r1test::finish();
}
