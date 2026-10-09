// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for Toolbar and FlyoutList: the measured geometry of the editor's tool bar (344 px
//   for the reference button set: 32 px buttons, 12 px flyout chevrons, 4 px separator, gap 2,
//   padding 4 plus border), exclusive tool activation, actions and toggles, flyout groups (open,
//   keyboard and pointer pick, the main button takes the entry), tooltips with shortcuts, roving
//   keyboard navigation, the vertical orientation, and hostile input (empty groups, 2000 buttons,
//   a callback that destroys the toolbar, disabling during a press, invalid UTF-8 labels).
// Callers: CTest (toolbar fast, no GPU).
#include <string>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/toolbar/Toolbar.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Button;
using r1ui::core::events::Key;
namespace layout = r1ui::core::layout;
using r1ui::core::tree::WidgetId;

struct Rig {
  explicit Rig(ToolbarOrientation o = ToolbarOrientation::Horizontal) : t(900, 700) {
    t.ui.rootStyle().direction = layout::FlexDirection::Column;
    t.ui.rootStyle().alignItems = layout::Align::Start;
    t.ui.rootStyle().justifyContent = layout::Justify::End;  // at the bottom, like the reference: flyouts open above
    bar = &t.ui.create<Toolbar>(t.ui.root(), o);
  }
  void paint() {
    r1ui::render::Painter painter;
    painter.begin(900, 700);
    t.ui.paint(painter);
    t.ui.finishPaint();
    painter.end();
  }
  void click(const layout::Rect& r, Button b = Button::Left) {
    const double x = r.x + r.w / 2.0, y = r.y + r.h / 2.0;
    t.ui.pointerMove(x, y);
    t.ui.pointerDown(x, y, b);
    t.ui.pointerUp(x, y, b);
  }
  layout::Rect rect(WidgetId id) { return t.ui.absRect(id); }
  r1test::TestUi t;
  Toolbar* bar = nullptr;
};

// The tool set of the reference screenshot.
void buildReference(Rig& rig, std::vector<std::string>* log = nullptr) {
  Toolbar& bar = *rig.bar;
  if (log != nullptr) bar.setOnTool([log](const std::string& id) { log->push_back(id); });
  bar.addTool("select", "mouse-pointer", "Select (V)");
  bar.addToolGroup({{"frame", "frame", "Frame", "F"}, {"section", "layout-grid", "Section", "S"}});
  bar.addToolGroup({{"rect", "square", "Rectangle", "R"}, {"ellipse", "circle", "Ellipse", "O"}});
  bar.addTool("pen", "pen-tool", "Pen (P)");
  bar.addTool("text", "type", "Text (T)");
  bar.addTool("hand", "hand", "Hand (H)");
  bar.addSeparator();
  bar.addAction("components", "component", "Components", [](ToolbarButton&) {});
  bar.addAction("grid", "grid-3x3", "Grid", [](ToolbarButton&) {});
  bar.addToggle("theme", "moon", "Theme", [](ToolbarButton&) {});
  rig.t.layout();
}

void testGeometry() {
  Rig rig;
  buildReference(rig);
  const layout::Rect r = rig.rect(rig.bar->id());
  R1_EXPECT(r.w == 344 && r.h == 42);  // measured: border 1 + padding 4 + 316 of items and 18 of gaps
  const layout::Rect first = rig.rect(rig.bar->button(0)->id());
  R1_EXPECT(first.w == 32 && first.h == 32 && first.x == r.x + 5 && first.y == r.y + 5);
  const layout::Rect frame = rig.rect(rig.bar->button(1)->id());
  R1_EXPECT(frame.x == first.x + 32 + 2);
  const layout::Rect rectBtn = rig.rect(rig.bar->button(2)->id());
  R1_EXPECT(rectBtn.x == frame.x + 32 + 12 + 2);  // chevron directly after the button, then the gap
  R1_EXPECT(rig.bar->buttonCount() == 9);
  rig.paint();
  // Vertical: 32 px wide buttons stacked, trigger below the button.
  Rig vertical(ToolbarOrientation::Vertical);
  vertical.bar->addTool("a", "square", "A");
  vertical.bar->addToolGroup({{"b", "circle", "B", ""}, {"c", "frame", "C", ""}});
  vertical.bar->addSeparator();
  vertical.bar->addAction("d", "plus", "D", [](ToolbarButton&) {});
  vertical.t.layout();
  const layout::Rect vr = vertical.rect(vertical.bar->id());
  R1_EXPECT(vr.w == 42 && vr.h == 10 + 32 + 2 + (32 + 12) + 2 + 4 + 2 + 32);
  vertical.paint();
}

void testToolsAndCallbacks() {
  Rig rig;
  std::vector<std::string> log;
  buildReference(rig, &log);
  R1_EXPECT(rig.bar->activeTool().empty());
  rig.click(rig.rect(rig.bar->button(0)->id()));
  R1_EXPECT(log.size() == 1 && log[0] == "select" && rig.bar->activeTool() == "select" && rig.bar->button(0)->active());
  rig.click(rig.rect(rig.bar->button(3)->id()));  // the pen
  R1_EXPECT(log.back() == "pen" && rig.bar->activeTool() == "pen" && !rig.bar->button(0)->active());
  R1_EXPECT(rig.bar->setActiveTool("hand") && rig.bar->activeTool() == "hand");  // programmatic: no callback
  R1_EXPECT(log.size() == 2);
  R1_EXPECT(!rig.bar->setActiveTool("nonexistent") && rig.bar->activeTool() == "hand");
  R1_EXPECT(rig.bar->setActiveTool("ellipse") && rig.bar->activeTool() == "ellipse");  // an entry of a flyout group
  R1_EXPECT(rig.bar->button(2)->toolId() == "ellipse" && rig.bar->button(2)->icon() == "circle" && rig.bar->button(2)->tooltipText() == "Ellipse (O)");
  // Actions never become the active tool; a toggle flips.
  rig.click(rig.rect(rig.bar->button(6)->id()));
  R1_EXPECT(rig.bar->activeTool() == "ellipse" && !rig.bar->button(6)->active());
  const layout::Rect theme = rig.rect(rig.bar->button(8)->id());
  rig.click(theme);
  R1_EXPECT(rig.bar->button(8)->active());
  rig.click(theme);
  R1_EXPECT(!rig.bar->button(8)->active());
  // Press, leave, release: nothing happens.
  const size_t before = log.size();
  const layout::Rect hand = rig.rect(rig.bar->button(5)->id());
  rig.t.ui.pointerMove(hand.x + 5, hand.y + 5);
  rig.t.ui.pointerDown(hand.x + 5, hand.y + 5);
  rig.t.ui.pointerMove(hand.x + 100, hand.y + 100);
  rig.t.ui.pointerUp(hand.x + 100, hand.y + 100);
  R1_EXPECT(log.size() == before);
  // Tooltips carry the shortcut text.
  R1_EXPECT(rig.bar->button(3)->tooltipText() == "Pen (P)" && rig.bar->button(3)->accessibleName() == "Pen (P)");
  // Disabled buttons do nothing and look disabled; disabling during a press cancels the click.
  rig.bar->button(3)->setEnabled(false);
  rig.click(rig.rect(rig.bar->button(3)->id()));
  R1_EXPECT(log.size() == before && rig.bar->button(3)->paintOpacity() == 0.5f);
  rig.bar->button(3)->setEnabled(true);
  const layout::Rect text = rig.rect(rig.bar->button(4)->id());
  rig.t.ui.pointerMove(text.x + 5, text.y + 5);
  rig.t.ui.pointerDown(text.x + 5, text.y + 5);
  rig.bar->button(4)->setEnabled(false);
  rig.t.ui.pointerUp(text.x + 5, text.y + 5);
  R1_EXPECT(log.size() == before);
  rig.paint();
}

void testFlyout() {
  Rig rig;
  std::vector<std::string> log;
  buildReference(rig, &log);
  // The chevron of the second group (rectangle) opens its list above the toolbar.
  const layout::Rect group = rig.rect(rig.bar->button(2)->id());
  rig.click({group.x + 32, group.y, 12, 32});
  R1_EXPECT(rig.t.ui.overlays().count() == 1);
  rig.t.layout();
  rig.paint();
  const layout::Rect bar = rig.rect(rig.bar->id());
  const layout::Rect host = rig.rect(rig.t.ui.overlays().hostOf(rig.t.ui.overlays().topmost()));
  R1_EXPECT(host.y + host.h <= bar.y);  // placed above the bar
  // Keyboard: the list has focus; Down moves, Enter picks and closes it.
  rig.t.ui.keyDown(Key::Down);
  rig.t.ui.keyDown(Key::Enter);
  R1_EXPECT(rig.t.ui.overlays().count() == 0);
  R1_EXPECT(log.size() == 1 && log[0] == "ellipse" && rig.bar->activeTool() == "ellipse");
  // Pointer pick of the first entry of the first group.
  const layout::Rect g1 = rig.rect(rig.bar->button(1)->id());
  rig.click({g1.x + 32, g1.y, 12, 32});
  rig.t.layout();
  const WidgetId list = rig.t.ui.tree().firstChild(rig.t.ui.overlays().hostOf(rig.t.ui.overlays().topmost()));
  const layout::Rect l = rig.rect(list);
  rig.click({l.x + 4, l.y + 28 + 4, l.w - 8, 20});  // the second row: "Section"
  R1_EXPECT(rig.t.ui.overlays().count() == 0);
  R1_EXPECT(log.back() == "section" && rig.bar->button(1)->icon() == "layout-grid" && rig.bar->button(1)->tooltipText() == "Section (S)");
  // Escape and outside presses close it without a pick.
  rig.click({g1.x + 32, g1.y, 12, 32});
  R1_EXPECT(rig.t.ui.overlays().count() == 1);
  rig.t.ui.keyDown(Key::Escape);
  R1_EXPECT(rig.t.ui.overlays().count() == 0 && log.back() == "section");
  rig.click({g1.x + 32, g1.y, 12, 32});
  rig.t.ui.pointerDown(700, 600);
  rig.t.ui.pointerUp(700, 600);
  R1_EXPECT(rig.t.ui.overlays().count() == 0);

  // FlyoutList on its own: separators and disabled rows are skipped, Home / End, Up / Down do not wrap.
  UiContext& ui = rig.t.ui;
  std::vector<size_t> picks;
  std::vector<FlyoutItem> items(5);
  items[0].label = "One";
  items[1].separator = true;
  items[2].label = "Disabled";
  items[2].enabled = false;
  items[3].label = "Three";
  items[3].shortcut = "Ctrl+3";
  items[4].label = "\xFF\xFE broken";
  const OverlayHandle h = openFlyout(ui, items, [&](size_t i) { picks.push_back(i); }, FlyoutOpenOptions{.anchor = {100, 100, 20, 20}, .placement = Placement::BelowStart});
  R1_EXPECT(h.valid());
  rig.t.layout();
  FlyoutList* flyout = ui.objectAs<FlyoutList>(ui.tree().firstChild(h.host));
  R1_EXPECT(flyout != nullptr && flyout->itemCount() == 5 && flyout->highlighted() == -1);
  ui.keyDown(Key::Down);
  R1_EXPECT(flyout->highlighted() == 0);
  ui.keyDown(Key::Down);
  R1_EXPECT(flyout->highlighted() == 3);  // skipped the separator and the disabled row
  ui.keyDown(Key::Down);
  ui.keyDown(Key::Down);
  R1_EXPECT(flyout->highlighted() == 4);  // no wrap
  ui.keyDown(Key::Home);
  R1_EXPECT(flyout->highlighted() == 0);
  ui.keyDown(Key::Up);
  R1_EXPECT(flyout->highlighted() == 0);
  ui.keyDown(Key::End);
  R1_EXPECT(flyout->highlighted() == 4);
  R1_EXPECT(!flyout->pick(1) && !flyout->pick(2) && !flyout->pick(99));
  rig.paint();
  ui.keyDown(Key::Space);
  R1_EXPECT(picks.size() == 1 && picks[0] == 4 && ui.overlays().count() == 0);
  FlyoutOpenOptions none;
  R1_EXPECT(!openFlyout(ui, {}, [](size_t) {}, none).valid());
}

void testKeyboardNavigation() {
  Rig rig;
  buildReference(rig);
  R1_EXPECT(!rig.t.ui.router().focus(rig.bar->button(0)->id()));  // not focusable by default
  rig.bar->setKeyboardNavigation(true);
  R1_EXPECT(rig.t.ui.router().focus(rig.bar->button(0)->id(), r1ui::core::events::FocusReason::Keyboard));
  R1_EXPECT(rig.t.ui.keyDown(Key::Right));
  R1_EXPECT(rig.t.ui.router().focused() == rig.bar->button(1)->id());
  R1_EXPECT(rig.t.ui.keyDown(Key::Right));  // the chevron of the first group
  R1_EXPECT(rig.t.ui.router().focused() != rig.bar->button(2)->id());
  R1_EXPECT(rig.t.ui.keyDown(Key::Right));
  R1_EXPECT(rig.t.ui.router().focused() == rig.bar->button(2)->id());
  R1_EXPECT(rig.t.ui.keyDown(Key::End));
  R1_EXPECT(rig.t.ui.router().focused() == rig.bar->button(8)->id());
  R1_EXPECT(rig.t.ui.keyDown(Key::Home));
  R1_EXPECT(rig.t.ui.router().focused() == rig.bar->button(0)->id());
  rig.bar->setActiveTool("select");
  rig.t.ui.keyDown(Key::Right);
  rig.t.ui.keyDown(Key::Space);
  rig.t.ui.keyUp(Key::Space);
  R1_EXPECT(rig.bar->activeTool() == "frame");
  // Disabled buttons are skipped by the arrows.
  rig.bar->button(2)->setEnabled(false);
  rig.t.ui.router().focus(rig.bar->button(1)->id(), r1ui::core::events::FocusReason::Keyboard);
  rig.t.ui.keyDown(Key::Right);
  rig.t.ui.keyDown(Key::Right);
  R1_EXPECT(rig.t.ui.router().focused() != rig.bar->button(2)->id());
  // The chevron opens its list from the keyboard.
  rig.t.ui.router().focus(rig.bar->button(1)->id(), r1ui::core::events::FocusReason::Keyboard);
  rig.t.ui.keyDown(Key::Right);
  rig.t.ui.keyDown(Key::Enter);
  R1_EXPECT(rig.t.ui.overlays().count() == 1);
  rig.paint();
  rig.bar->setKeyboardNavigation(false);
}

void testHostile() {
  Rig rig;
  R1_EXPECT(&rig.bar->addToolGroup({}) != nullptr);  // an empty group is a plain tool
  rig.bar->addTool("\xFF\xFE", "no-such-icon", std::string(5000, 'x'));
  for (int i = 0; i < 2000; ++i) rig.bar->addAction("a" + std::to_string(i), "plus", "Action " + std::to_string(i), [](ToolbarButton&) {});
  rig.t.layout();
  bool threw = false;
  try {
    rig.paint();
  } catch (const std::runtime_error&) {
    threw = true;  // an unknown icon is reported by the icon cache (the shell shows it), never a crash
  }
  R1_EXPECT(threw);
  rig.bar->button(1)->setIcon("plus");
  rig.paint();
  R1_EXPECT(rig.bar->buttonCount() == 2002);
  R1_EXPECT(rig.bar->button(5000) == nullptr);
  R1_EXPECT(rig.bar->setActiveTool("\xFF\xFE") && rig.bar->activeTool() == "\xFF\xFE");
  // A callback that destroys the toolbar during activation and during a flyout pick.
  Rig doomed;
  doomed.bar->addTool("x", "square", "X");
  doomed.bar->addToolGroup({{"g1", "frame", "G1", ""}, {"g2", "circle", "G2", ""}});
  const WidgetId id = doomed.bar->id();
  doomed.bar->setOnTool([&](const std::string&) { doomed.t.ui.destroy(id); });
  doomed.t.layout();
  doomed.click(doomed.rect(doomed.bar->button(0)->id()));
  R1_EXPECT(!doomed.t.ui.alive(id));
  doomed.t.layout();
  doomed.paint();
  Rig doomed2;
  doomed2.bar->addToolGroup({{"g1", "frame", "G1", ""}, {"g2", "circle", "G2", ""}});
  const WidgetId id2 = doomed2.bar->id();
  doomed2.bar->setOnTool([&](const std::string&) { doomed2.t.ui.destroy(id2); });
  doomed2.t.layout();
  const layout::Rect g = doomed2.rect(doomed2.bar->button(0)->id());
  doomed2.click({g.x + 32, g.y, 12, 32});
  doomed2.t.layout();
  doomed2.t.ui.keyDown(Key::Down);
  doomed2.t.ui.keyDown(Key::Enter);
  R1_EXPECT(!doomed2.t.ui.alive(id2) && doomed2.t.ui.overlays().count() == 0);
  doomed2.t.layout();
  // The toolbar is destroyed while its flyout is open.
  Rig open;
  open.bar->addToolGroup({{"g1", "frame", "G1", ""}, {"g2", "circle", "G2", ""}});
  open.t.layout();
  const layout::Rect og = open.rect(open.bar->button(0)->id());
  open.click({og.x + 32, og.y, 12, 32});
  open.t.ui.destroy(open.bar->id());
  open.t.layout();
  open.t.ui.keyDown(Key::Enter);
  open.paint();
}

}  // namespace

int main() {
  testGeometry();
  testToolsAndCallbacks();
  testFlyout();
  testKeyboardNavigation();
  testHostile();
  return r1test::finish();
}
