// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tests of the drawn keyboard on its own: which caps count as assigned for each modifier layer
//   (exactly the chords with those modifiers), the category and context filters, sequences on their first
//   key, the selected command's key, the physical and toggled modifier sources and how they combine,
//   hit-testing against the layout, the hover tooltip text, clicks (modifier caps toggle, bindable caps
//   report, unbindable caps are ignored), geometry at several sizes, and a paint pass.
// Callers: CTest (label fast).
#include "HotkeyFixture.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;

struct KeyboardScene {
  KeyboardScene() : s(true) {}
  HotkeyScene s;
  KeyboardView& kb() { return s.editor->keyboard(); }
};

void testAssignedPerLayer() {
  KeyboardScene k;
  KeyboardView& kb = k.kb();
  // No modifiers: F, Delete and Backspace carry a hotkey (the layers panel's Delete shares the key).
  R1_EXPECT(kb.assigned(letter('F')) && kb.assigned(Key::Delete) && kb.assigned(Key::Backspace));
  R1_EXPECT(!kb.assigned(letter('C')) && !kb.assigned(letter('S')));
  R1_EXPECT(kb.usesOf(Key::Delete).size() == 2);  // global.delete and layers.delete
  kb.setToggledModifiers(Mod::kCtrl);
  R1_EXPECT(kb.assigned(letter('C')) && kb.assigned(letter('V')) && kb.assigned(letter('Z')) && kb.assigned(letter('S')) && kb.assigned(letter('O')));
  R1_EXPECT(kb.assigned(letter('K')));  // first key of the sequence Ctrl+K, Ctrl+C
  R1_EXPECT(kb.usesOf(letter('K')).size() == 1 && kb.usesOf(letter('K'))[0].startsSequence);
  R1_EXPECT(!kb.assigned(letter('F')) && !kb.assigned(Key::Delete));
  kb.setToggledModifiers(Mod::kCtrl | Mod::kShift);
  R1_EXPECT(kb.assigned(letter('Z')) && !kb.assigned(letter('C')) && kb.assignedKeyCount() == 1);  // only Redo
  kb.setToggledModifiers(Mod::kAlt);
  R1_EXPECT(kb.assignedKeyCount() == 0);
  kb.setToggledModifiers(0);
  R1_EXPECT(kb.assignedKeyCount() == 3);  // F, Delete, Backspace
}

void testSources() {
  KeyboardScene k;
  KeyboardView& kb = k.kb();
  kb.setPhysicalModifiers(Mod::kCtrl);
  R1_EXPECT(kb.effectiveModifiers() == Mod::kCtrl && kb.assigned(letter('C')));
  kb.setToggledModifiers(Mod::kShift);
  R1_EXPECT(kb.effectiveModifiers() == (Mod::kCtrl | Mod::kShift) && kb.assigned(letter('Z')) && !kb.assigned(letter('C')));
  kb.setPhysicalModifiers(0);
  R1_EXPECT(kb.effectiveModifiers() == Mod::kShift);
  kb.setToggledModifiers(0);
  // Bits outside the four modifiers are dropped.
  kb.setPhysicalModifiers(0xF0 | Mod::kAlt);
  R1_EXPECT(kb.effectiveModifiers() == Mod::kAlt);
  kb.setPhysicalModifiers(0);
  // Key events that reach the keyboard carry the held modifiers; losing focus clears them.
  k.s.t.ui.focusWidget(kb.id(), r1ui::core::events::FocusReason::Keyboard);
  k.s.t.ui.keyDown(Key::Unknown, Mod::kCtrl);
  R1_EXPECT(kb.physicalModifiers() == Mod::kCtrl && kb.assigned(letter('S')));
  k.s.t.ui.keyDown(Key::Unknown, Mod::kCtrl | Mod::kShift);
  R1_EXPECT(kb.effectiveModifiers() == (Mod::kCtrl | Mod::kShift));
  k.s.t.ui.keyUp(Key::Unknown, Mod::kCtrl);
  R1_EXPECT(kb.physicalModifiers() == Mod::kCtrl);
  k.s.t.ui.keyUp(Key::Unknown, 0);
  R1_EXPECT(kb.physicalModifiers() == 0);
  k.s.t.ui.keyDown(Key::Unknown, Mod::kAlt);
  k.s.t.ui.focusWidget(k.s.editor->list().searchField());
  R1_EXPECT(kb.physicalModifiers() == 0);
  // Modifier events that reach the editor from another focused widget update the layer too.
  k.s.t.ui.keyDown(Key::Unknown, Mod::kCtrl);
  R1_EXPECT(kb.physicalModifiers() == Mod::kCtrl);
  k.s.t.ui.keyUp(Key::Unknown, 0);
  R1_EXPECT(kb.physicalModifiers() == 0);
}

void testFilters() {
  KeyboardScene k;
  KeyboardView& kb = k.kb();
  kb.setToggledModifiers(Mod::kCtrl);
  kb.setUsageFilter({"File", ""});
  R1_EXPECT(kb.assigned(letter('S')) && kb.assigned(letter('O')) && !kb.assigned(letter('C')));
  kb.setUsageFilter({"Edit", ""});
  R1_EXPECT(kb.assigned(letter('C')) && !kb.assigned(letter('S')));
  kb.setUsageFilter({"No such category", ""});
  R1_EXPECT(kb.assignedKeyCount() == 0);
  kb.setUsageFilter({"", "layers"});
  R1_EXPECT(kb.assigned(letter('C')));  // a panel context also has the global commands
  kb.setToggledModifiers(0);
  R1_EXPECT(kb.usesOf(Key::Delete).size() == 2);
  kb.setUsageFilter({"", "global"});
  R1_EXPECT(kb.usesOf(Key::Delete).size() == 1);
  kb.setUsageFilter({});
}

void testSelectedAndLive() {
  KeyboardScene k;
  KeyboardView& kb = k.kb();
  kb.setToggledModifiers(Mod::kCtrl);
  kb.setSelectedCommand("edit.copy");
  R1_EXPECT(kb.holdsSelected(letter('C')) && !kb.holdsSelected(letter('V')));
  kb.setSelectedCommand("");
  R1_EXPECT(!kb.holdsSelected(letter('C')));
  // A rebinding shows at once (the registry notifies, the editor refreshes the keyboard).
  cmd::assignChord(k.s.overrides, k.s.keymap, k.s.registry, "file.save", 0, chordOf(letter('J'), Mod::kCtrl), false);
  k.s.t.layout();
  R1_EXPECT(kb.assigned(letter('J')) && !kb.assigned(letter('S')));
}

void testGeometryAndHits() {
  KeyboardScene k;
  KeyboardView& kb = k.kb();
  const auto& caps = kb.layout().caps();
  const auto area = k.s.t.ui.absRect(kb.id());
  R1_EXPECT(area.w > 300 && area.h > 100);
  for (size_t i = 0; i < caps.size(); ++i) {
    const RectD r = kb.capRect(i);
    R1_EXPECT(r.w > 0.0 && r.h > 0.0);
    R1_EXPECT(r.x >= area.x - 0.5 && r.x + r.w <= area.x + area.w + 0.5 && r.y >= area.y - 0.5 && r.y + r.h <= area.y + area.h + 0.5);  // inside the widget
    R1_EXPECT(kb.capAt(r.x + r.w / 2, r.y + r.h / 2) == static_cast<int>(i));
  }
  R1_EXPECT(kb.capAt(area.x - 50, area.y - 50) == -1 && kb.capAt(std::nan(""), 3.0) == -1);
  R1_EXPECT(kb.capRect(caps.size()).w == 0.0);
  // The board keeps its aspect ratio when the editor is resized.
  k.s.editor->style().width = r1ui::core::layout::Length::px(1000);
  k.s.editor->requestLayout();
  k.s.t.layout();
  const auto narrow = k.s.t.ui.absRect(kb.id());
  R1_EXPECT(narrow.w < area.w);
  k.s.paintOnce();
}

void testTooltipAndClicks() {
  KeyboardScene k;
  KeyboardView& kb = k.kb();
  std::vector<std::pair<Key, uint8_t>> clicked;
  kb.setOnKeyClicked([&](Key key, uint8_t m) { clicked.push_back({key, m}); });
  // Hover text: unassigned, assigned, modifier, unbindable.
  const auto hover = [&](const char* id) {
    const RectD r = k.s.cap(id);
    k.s.t.ui.pointerMove(r.x + r.w / 2, r.y + r.h / 2);
    return std::string(kb.tooltipText());
  };
  R1_EXPECT(hover("q") == "Q: unassigned");
  R1_EXPECT(hover("f") == "F: Fit to window");
  kb.setToggledModifiers(Mod::kCtrl);
  R1_EXPECT(hover("c") == "Ctrl+C: Copy");
  R1_EXPECT(hover("k") == "Ctrl+K, Ctrl+C: Comment line");
  R1_EXPECT(hover("q") == "Ctrl+Q: unassigned");
  kb.setToggledModifiers(0);
  R1_EXPECT(hover("delete").find("Delete: Delete\n") == 0 && hover("delete").find("Delete: Delete layer [layers]") != std::string::npos);
  R1_EXPECT(hover("lshift").find("Shift") == 0);
  R1_EXPECT(hover("grave").find("cannot hold") != std::string::npos);
  k.s.t.ui.pointerMove(0, 0);
  R1_EXPECT(kb.tooltipText().empty());
  // Clicks.
  k.s.clickCap("grave");   // no Key code: ignored
  k.s.clickCap("caps");    // ignored
  R1_EXPECT(clicked.empty());
  k.s.clickCap("rctrl");   // a modifier cap toggles the layer
  R1_EXPECT(kb.toggledModifiers() == Mod::kCtrl && clicked.empty());
  k.s.clickCap("lctrl");   // either side of the board toggles the same modifier
  R1_EXPECT(kb.toggledModifiers() == 0);
  k.s.clickCap("lalt");
  k.s.clickCap("rshift");
  R1_EXPECT(kb.toggledModifiers() == (Mod::kAlt | Mod::kShift));
  k.s.clickCap("x");
  R1_EXPECT(clicked.size() == 1 && clicked[0].first == letter('X') && clicked[0].second == (Mod::kAlt | Mod::kShift));
  // A right click does nothing.
  const RectD r = k.s.cap("x");
  k.s.t.ui.pointerMove(r.x + 5, r.y + 5);
  k.s.t.ui.pointerDown(r.x + 5, r.y + 5, r1ui::core::events::Button::Right);
  k.s.t.ui.pointerUp(r.x + 5, r.y + 5, r1ui::core::events::Button::Right);
  R1_EXPECT(clicked.size() == 1);
}

}  // namespace

int main() {
  testAssignedPerLayer();
  testSources();
  testFilters();
  testSelectedAndLive();
  testGeometryAndHits();
  testTooltipAndClicks();
  return r1test::finish();
}
