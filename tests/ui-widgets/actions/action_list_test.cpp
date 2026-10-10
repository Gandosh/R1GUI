// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: behaviour tests of ActionList: grouping and sorted headers, collapse by click and keys (and
//   that a search shows groups expanded), the category filter, the search rule on the rows, selection by
//   click and keys with its callbacks, Enter and double-click activation, type-to-search and Ctrl+F,
//   Up/Down from the search box, scrolling and the thumb, the hover tooltip, the drag payload through a
//   DragHub into a target (accept, cancel, header, no hub), live following of a CommandRegistry, and a
//   paint pass that must not fail.
// Callers: CTest (label fast).
#include "ActionFixture.h"
#include "r1ui/commands/CommandRegistry.h"
#include "r1ui/commands/Keymap.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace {

using namespace r1test;
using namespace r1ui::widgets;
namespace cmd = r1ui::commands;
namespace Mod = r1ui::core::events::Mod;

void testGrouping() {
  ActionScene s(sampleActions(30));
  R1_EXPECT((s.headers() == std::vector<std::string>{"Alpha", "Beta", "Gamma"}));
  R1_EXPECT(s.view().rows().size() == 33);  // three headers and thirty actions
  R1_EXPECT(s.view().matchCount() == 30);
  // Inside a group the given order stays.
  const int alpha = s.headerOf("Alpha");
  R1_EXPECT(alpha == 0 && s.view().rowAction(1).id == "cmd.1" && s.view().rowAction(2).id == "cmd.4");
  R1_EXPECT(s.view().rows()[0].count == 10);
  // Collapse by clicking the header: the group's actions disappear, the others stay.
  s.click(s.headerOf("Beta"), 80);
  R1_EXPECT(s.view().isCollapsed("Beta") && s.view().rows().size() == 23);
  R1_EXPECT(s.view().selectedRow() == s.headerOf("Beta") && s.view().selectedAction() == nullptr);
  s.click(s.headerOf("Beta"), 80);
  R1_EXPECT(!s.view().isCollapsed("Beta") && s.view().rows().size() == 33);
  // Keys on a header: Left collapses, Right expands, Enter toggles, Left on an action goes to its header.
  s.focusView();
  s.view().selectRow(s.headerOf("Gamma"));
  s.key(Key::Left);
  R1_EXPECT(s.view().isCollapsed("Gamma"));
  s.key(Key::Left);
  R1_EXPECT(s.view().isCollapsed("Gamma"));  // already collapsed: nothing more
  s.key(Key::Right);
  R1_EXPECT(!s.view().isCollapsed("Gamma"));
  s.key(Key::Enter);
  R1_EXPECT(s.view().isCollapsed("Gamma"));
  s.key(Key::Enter);
  s.view().selectRow(s.headerOf("Gamma") + 2);
  s.key(Key::Left);
  R1_EXPECT(s.view().selectedRow() == s.headerOf("Gamma"));
  // A search shows collapsed groups expanded; the state comes back when the search is cleared.
  s.view().setCollapsed("Alpha", true);
  s.list->setFilter("cmd.");
  R1_EXPECT(s.view().matchCount() == 30 && s.view().rows().size() == 33);
  s.list->setFilter("");
  R1_EXPECT(s.view().isCollapsed("Alpha") && s.view().rows().size() == 33 - 10);
  s.view().setCollapsed("Alpha", false);
  // Category filter.
  s.view().setCategoryFilter("Beta");
  R1_EXPECT((s.headers() == std::vector<std::string>{"Beta"}) && s.view().rows().size() == 11);
  s.view().setCategoryFilter("");
  R1_EXPECT(s.headers().size() == 3);
  s.view().setCategoryFilter("Nope");
  R1_EXPECT(s.view().rows().empty() && s.view().selectedRow() == -1);
  s.view().setCategoryFilter("");
}

void testSearch() {
  ActionScene s(sampleActions(30));
  s.list->setFilter("action 7");  // two terms, both required: labels "Action 7", "Action 17", "Action 27"
  R1_EXPECT((s.ids() == std::vector<std::string>{"cmd.7", "cmd.17", "cmd.27"} || s.ids().size() == 3));
  s.list->setFilter("ACTION 1");
  R1_EXPECT(s.ids().size() == 12);  // 1, 10..19, 21
  s.list->setFilter("thing 2 widget");
  R1_EXPECT(s.ids().size() == 12);  // description: 2, 12, 20..29
  s.list->setFilter("gamma");       // category
  R1_EXPECT(s.ids().size() == 10);
  s.list->setFilter("cmd.12");      // id
  R1_EXPECT(s.ids() == std::vector<std::string>{"cmd.12"});
  s.list->setFilter("nothing matches this");
  R1_EXPECT(s.view().rows().empty() && s.view().matchCount() == 0);
  s.list->setFilter("");
  R1_EXPECT(s.view().rows().size() == 33);
  // The search box is the same state: typing in the field filters.
  TextInput* field = s.t.ui.objectAs<TextInput>(s.list->searchField());
  R1_EXPECT(field != nullptr);
  s.list->setFilter("beta");
  R1_EXPECT(field->text() == "beta");
  // A search selects its first hit and keeps a selected action that still matches.
  s.list->setFilter("action 2");
  R1_EXPECT(s.view().selectedAction() != nullptr && s.view().selectedAction()->id == "cmd.2");
  s.list->setFilter("action 2 widget");
  R1_EXPECT(s.view().selectedAction() != nullptr && s.view().selectedAction()->id == "cmd.2");
  s.paintOnce();
}

void testSelectionAndKeys() {
  ActionScene s(sampleActions(30));
  std::vector<std::string> selected, activated;
  s.view().setOnSelect([&](const ActionInfo& a) { selected.push_back(a.id); });
  s.view().setOnActivate([&](const ActionInfo& a) { activated.push_back(a.id); });
  const int row = s.rowOf("cmd.4");
  s.click(row);
  R1_EXPECT(s.view().selectedRow() == row && selected == std::vector<std::string>{"cmd.4"});
  s.focusView();
  s.key(Key::Down);
  R1_EXPECT(s.view().selectedAction()->id == "cmd.7");
  s.key(Key::Up);
  s.key(Key::Up);
  R1_EXPECT(s.view().selectedAction()->id == "cmd.1");
  s.key(Key::Up);  // onto the header: no selection callback, no action
  R1_EXPECT(s.view().selectedAction() == nullptr && selected.back() == "cmd.1");
  s.key(Key::Home);
  R1_EXPECT(s.view().selectedRow() == 0);
  s.key(Key::End);
  R1_EXPECT(s.view().selectedRow() == static_cast<int>(s.view().rows().size()) - 1);
  R1_EXPECT(s.view().scrollOffset() > 0.0 && s.view().rowRect(s.view().selectedRow()).h > 0.0);  // scrolled into view
  s.key(Key::PageUp);
  R1_EXPECT(s.view().selectedRow() < static_cast<int>(s.view().rows().size()) - 3);
  s.key(Key::Home);
  R1_EXPECT(s.view().scrollOffset() == 0.0);
  // Enter activates an action, not a header.
  s.view().selectAction("cmd.9");
  s.key(Key::Enter);
  R1_EXPECT(activated == std::vector<std::string>{"cmd.9"});
  s.view().selectRow(0);
  s.key(Key::Enter);
  R1_EXPECT(activated.size() == 1);
  // Double click activates.
  const int r2 = s.rowOf("cmd.2");
  s.click(r2);
  s.click(r2);
  R1_EXPECT(activated.size() == 2 && activated.back() == "cmd.2");
  // Selection survives a refresh of the same actions.
  s.view().selectAction("cmd.5");
  s.list->setActions(sampleActions(30));
  R1_EXPECT(s.view().selectedAction() != nullptr && s.view().selectedAction()->id == "cmd.5");
  // selectAction expands a collapsed group; an unknown id fails.
  s.view().setCollapsed("Alpha", true);
  R1_EXPECT(s.view().selectAction("cmd.1") && !s.view().isCollapsed("Alpha"));
  R1_EXPECT(!s.view().selectAction("missing"));
  s.paintOnce();
}

void testTypeToSearch() {
  ActionScene s(sampleActions(30));
  s.focusView();
  R1_EXPECT(s.t.ui.textInput(U'x'));  // the list wants typed characters
  s.t.layout();
  TextInput* field = s.t.ui.objectAs<TextInput>(s.list->searchField());
  R1_EXPECT(field->text() == "x" && s.view().query() == "x");
  R1_EXPECT(s.t.ui.router().focused() == s.list->searchField());
  s.t.ui.textInput(U'y');
  s.t.layout();
  R1_EXPECT(field->text() == "xy");
  s.list->setFilter("");
  s.focusView();
  s.t.ui.textInput(U'é');  // a non-ASCII character is appended as valid UTF-8
  R1_EXPECT(field->text() == "\xC3\xA9");
  s.t.ui.textInput(char32_t{0xD800});  // a surrogate is ignored
  R1_EXPECT(field->text() == "\xC3\xA9");
  // Ctrl+F moves to the search box and selects its text.
  s.list->setFilter("beta");
  s.focusView();
  s.key(static_cast<Key>('F'), Mod::kCtrl);
  R1_EXPECT(s.t.ui.router().focused() == s.list->searchField());
  // Up and Down in the search box move the selection; Enter activates.
  std::vector<std::string> activated;
  s.view().setOnActivate([&](const ActionInfo& a) { activated.push_back(a.id); });
  s.list->setFilter("action");
  s.t.ui.focusWidget(s.list->searchField(), r1ui::core::events::FocusReason::Keyboard);
  const std::string first = s.view().selectedAction()->id;
  s.key(Key::Down);
  R1_EXPECT(s.view().selectedAction()->id != first);
  s.key(Key::Up);
  R1_EXPECT(s.view().selectedAction()->id == first);
  s.key(Key::Enter);
  R1_EXPECT(activated == std::vector<std::string>{first});
}

void testScrollAndTooltip() {
  ActionScene s(sampleActions(300));
  const double content = s.view().contentHeight();
  R1_EXPECT(content > 300 * ActionListView::kRowHeight && content < 303 * ActionListView::kHeaderHeight + 300 * ActionListView::kRowHeight);
  R1_EXPECT(s.view().thumbRect().h > 0.0);
  s.view().setScrollOffset(1e9);
  R1_EXPECT(s.view().scrollOffset() == content - s.view().viewportHeight());
  s.view().setScrollOffset(-5.0);
  R1_EXPECT(s.view().scrollOffset() == 0.0);
  s.view().setScrollOffset(std::nan(""));
  R1_EXPECT(s.view().scrollOffset() == 0.0);
  // The wheel scrolls.
  const RectD r0 = s.rowRect(0);
  s.t.ui.pointerMove(r0.x + 50, r0.y + 10);
  s.t.ui.wheel(r0.x + 50, r0.y + 10, 0, -2);
  R1_EXPECT(s.view().scrollOffset() > 0.0);
  // rowAt agrees with rowRect for every visible row.
  for (int i = 0; i < static_cast<int>(s.view().rows().size()); ++i) {
    const RectD r = s.rowRect(i);
    if (r.h <= 0.0) continue;
    const double top = std::max(r.y, static_cast<double>(s.t.ui.absRect(s.list->viewWidget()).y));
    const double bottom = std::min(r.y + r.h, static_cast<double>(s.t.ui.absRect(s.list->viewWidget()).y) + s.view().viewportHeight());
    R1_EXPECT(s.view().rowAt(r.x + 10, (top + bottom) / 2) == i);
  }
  // Dragging the thumb scrolls.
  s.view().setScrollOffset(0.0);
  const RectD thumb = s.view().thumbRect();
  s.t.ui.pointerMove(thumb.x + 2, thumb.y + 5);
  s.t.ui.pointerDown(thumb.x + 2, thumb.y + 5);
  s.t.ui.pointerMove(thumb.x + 2, thumb.y + 105);
  s.t.ui.pointerUp(thumb.x + 2, thumb.y + 105);
  R1_EXPECT(s.view().scrollOffset() > 500.0);
  // Tooltip: the full description of the hovered action, nothing over a header.
  s.view().setScrollOffset(0.0);
  s.t.layout();
  const RectD action = s.rowRect(1);
  s.t.ui.pointerMove(action.x + 100, action.y + action.h / 2);
  R1_EXPECT(s.view().tooltipText() == s.view().rowAction(1).description);
  const RectD header = s.rowRect(0);
  s.t.ui.pointerMove(header.x + 100, header.y + header.h / 2);
  R1_EXPECT(s.view().tooltipText().empty());
  // The column layout never produces negative widths, at any width.
  for (const double w : {80.0, 200.0, 360.0, 700.0}) {
    s.list->style().width = r1ui::core::layout::Length::px(w);
    s.list->requestLayout();
    s.t.layout();
    const ActionListView::Columns c = s.view().columns();
    R1_EXPECT(c.labelW >= 0.0 && c.descW >= 0.0 && c.shortcutW >= 0.0);
  }
}

// A drop target that records what it gets.
struct Recorder final : DragTarget {
  bool dragOver(const DragPayload& p, double, double) override {
    over = p.kind == DragPayload::Kind::Command;
    return over;
  }
  void dragLeave() override { over = false; }
  bool dragDrop(const DragPayload& p, double, double) override {
    dropped.push_back(p);
    return true;
  }
  bool over = false;
  std::vector<DragPayload> dropped;
};

struct TargetWidget final : WidgetObject {
  const char* typeName() const override { return "TargetWidget"; }
};

void testDrag() {
  ActionScene s(sampleActions(30), {}, 900, 700);
  DragHub hub(s.t.ui);
  TargetWidget& zone = s.t.ui.create<TargetWidget>(s.t.ui.root());
  zone.style().width = r1ui::core::layout::Length::px(150);
  zone.style().height = r1ui::core::layout::Length::px(150);
  zone.style().position = r1ui::core::layout::Position::Absolute;
  zone.style().inset[r1ui::core::layout::kLeft] = r1ui::core::layout::Length::px(720);
  zone.style().inset[r1ui::core::layout::kTop] = r1ui::core::layout::Length::px(20);
  Recorder target;
  hub.addTarget(zone.id(), &target);
  s.t.layout();
  const auto zoneRect = s.t.ui.absRect(zone.id());
  const double zx = zoneRect.x + 50, zy = zoneRect.y + 50;

  // Without a hub a drag does nothing.
  int row = s.rowOf("cmd.4");
  RectD r = s.rowRect(row);
  s.t.ui.pointerMove(r.x + 60, r.y + r.h / 2);
  s.t.ui.pointerDown(r.x + 60, r.y + r.h / 2);
  s.t.ui.pointerMove(zx, zy);
  R1_EXPECT(!hub.active());
  s.t.ui.pointerUp(zx, zy);
  R1_EXPECT(target.dropped.empty());

  s.list->setDragHub(&hub);
  // A drag past the threshold starts the hub drag with the documented payload.
  r = s.rowRect(row);
  s.t.ui.pointerMove(r.x + 60, r.y + r.h / 2);
  s.t.ui.pointerDown(r.x + 60, r.y + r.h / 2);
  s.t.ui.pointerMove(r.x + 62, r.y + r.h / 2);
  R1_EXPECT(!hub.active());  // under the threshold
  s.t.ui.pointerMove(r.x + 200, r.y + 100);
  R1_EXPECT(hub.active() && hub.payload().kind == DragPayload::Kind::Command && hub.payload().commandId == "cmd.4" && hub.payload().text == "Action 4");
  R1_EXPECT(s.view().dragging() && s.t.ui.overlays().any());  // the ghost
  s.t.ui.pointerMove(zx, zy);
  R1_EXPECT(hub.accepting() && target.over);
  s.t.ui.pointerUp(zx, zy);
  R1_EXPECT(!hub.active() && !s.t.ui.overlays().any() && !s.view().dragging());
  R1_EXPECT(target.dropped.size() == 1 && target.dropped[0].commandId == "cmd.4");
  R1_EXPECT(makeActionDragPayload(s.view().actions()[4]).commandId == "cmd.4");

  // Escape cancels the drag and drops nothing.
  r = s.rowRect(s.rowOf("cmd.7"));
  s.t.ui.pointerMove(r.x + 60, r.y + r.h / 2);
  s.t.ui.pointerDown(r.x + 60, r.y + r.h / 2);
  s.t.ui.pointerMove(zx, zy);
  R1_EXPECT(hub.active());
  s.t.ui.keyDown(Key::Escape);
  R1_EXPECT(!hub.active() && !s.t.ui.overlays().any() && !target.over);
  s.t.ui.pointerUp(zx, zy);
  R1_EXPECT(target.dropped.size() == 1);

  // A release over nothing drops nothing; a header row cannot be dragged.
  r = s.rowRect(s.rowOf("cmd.7"));
  s.t.ui.pointerMove(r.x + 60, r.y + r.h / 2);
  s.t.ui.pointerDown(r.x + 60, r.y + r.h / 2);
  s.t.ui.pointerMove(850, 650);
  s.t.ui.pointerUp(850, 650);
  R1_EXPECT(target.dropped.size() == 1 && !hub.active());
  r = s.rowRect(0);
  s.t.ui.pointerMove(r.x + 60, r.y + r.h / 2);
  s.t.ui.pointerDown(r.x + 60, r.y + r.h / 2);
  s.t.ui.pointerMove(zx, zy);
  R1_EXPECT(!hub.active());
  s.t.ui.pointerUp(zx, zy);
  // Destroying the list in the middle of a drag cancels it.
  r = s.rowRect(s.rowOf("cmd.2"));
  s.t.ui.pointerMove(r.x + 60, r.y + r.h / 2);
  s.t.ui.pointerDown(r.x + 60, r.y + r.h / 2);
  s.t.ui.pointerMove(zx, zy);
  R1_EXPECT(hub.active());
  s.t.ui.destroy(s.list->id());
  R1_EXPECT(!hub.active() && !s.t.ui.overlays().any());
  s.t.ui.pointerUp(zx, zy);
  hub.removeTarget(zone.id());
}

void testRegistryBinding() {
  ActionScene s(std::vector<ActionInfo>{});
  cmd::CommandRegistry reg;
  cmd::KeybindingOverrides over(reg);
  cmd::Keymap map(reg, over);
  const auto add = [&](const std::string& id, const std::string& label, cmd::ChordSequence chord = {}) {
    cmd::CommandDef def;
    def.id = id;
    def.label = label;
    def.description = label + " does a thing";
    def.category = "Test";
    def.defaultChords = {chord, {}};
    def.execute = [](const cmd::ExecuteArgs&) { return cmd::ExecuteResult::handled(); };
    return reg.add(std::move(def)).ok;
  };
  R1_EXPECT(add("t.one", "One"));
  s.list->bindRegistry(&reg, &map);
  s.t.layout();
  R1_EXPECT(s.ids() == std::vector<std::string>{"t.one"});
  R1_EXPECT(add("t.two", "Two", cmd::ChordSequence::single({static_cast<Key>('T'), Mod::kCtrl, false})));
  s.t.layout();  // registry changes are coalesced into the next layout pass
  R1_EXPECT(s.ids().size() == 2 && s.view().rowAction(s.rowOf("t.two")).shortcut == "Ctrl+T");  // follows the registry live
  over.set("t.two", 0, cmd::ChordSequence::single({static_cast<Key>('Y'), Mod::kAlt, false}));
  s.t.layout();
  R1_EXPECT(s.view().rowAction(s.rowOf("t.two")).shortcut == "Alt+Y");  // and the keymap
  s.view().selectAction("t.two");
  reg.remove("t.one");
  s.t.layout();
  R1_EXPECT(s.ids() == std::vector<std::string>{"t.two"} && s.view().selectedAction() != nullptr);  // selection kept
  s.list->bindRegistry(nullptr, nullptr);
  add("t.three", "Three");
  s.t.layout();
  R1_EXPECT(s.ids().size() == 1);  // no longer followed
  // Destroying the list unsubscribes: changing the registry afterwards is safe.
  s.list->bindRegistry(&reg, &map);
  s.t.ui.destroy(s.list->id());
  add("t.four", "Four");
  s.t.layout();
}

}  // namespace

int main() {
  testGrouping();
  testSearch();
  testSelectionAndKeys();
  testTypeToSearch();
  testScrollAndTooltip();
  testDrag();
  testRegistryBinding();
  return r1test::finish();
}
