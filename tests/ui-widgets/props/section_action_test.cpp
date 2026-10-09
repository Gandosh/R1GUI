// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression test for a collapsible PropertySection with a header action: clicking the action
//   runs it and must not also collapse the section (the click bubbled to the header, which toggled),
//   while clicking the header itself still toggles. The generated property panel puts a reset-category
//   action in every collapsible header, which exposed the defect.
// Callers: CTest (props fast, no GPU).
#include "TestSupport.h"
#include "r1ui/widgets/section/Section.h"

namespace {

using namespace r1ui::widgets;
namespace layout = r1ui::core::layout;

void click(r1test::TestUi& t, r1ui::core::tree::WidgetId id) {
  t.ui.setTime(t.ui.now() + 1000);
  const layout::Rect r = t.ui.absRect(id);
  t.ui.pointerMove(r.x + r.w * 0.5, r.y + r.h * 0.5);
  t.ui.pointerDown(r.x + r.w * 0.5, r.y + r.h * 0.5);
  t.ui.pointerUp(r.x + r.w * 0.5, r.y + r.h * 0.5);
}

}  // namespace

int main() {
  r1test::TestUi t(300, 300);
  t.ui.rootStyle().alignItems = layout::Align::Start;
  PropertySection& section = t.ui.create<PropertySection>(t.ui.root(), "Appearance", SectionOptions{.collapsible = true});
  section.style().width = layout::Length::px(258);
  int activated = 0;
  ActionButton& action = section.addAction("plus", "Add", [&](ActionButton&) { ++activated; });
  t.layout();

  click(t, action.id());
  R1_EXPECT(activated == 1 && !section.collapsed());  // the action ran; the section stayed open

  // A click on the header's own area still toggles (the title is left of the action).
  const layout::Rect header = t.ui.absRect(section.header());
  t.ui.setTime(t.ui.now() + 1000);
  t.ui.pointerMove(header.x + 100, header.y + header.h * 0.5);
  t.ui.pointerDown(header.x + 100, header.y + header.h * 0.5);
  t.ui.pointerUp(header.x + 100, header.y + header.h * 0.5);
  R1_EXPECT(section.collapsed());
  click(t, action.id());
  R1_EXPECT(activated == 2 && section.collapsed());
  t.ui.setTime(t.ui.now() + 1000);
  t.ui.pointerMove(header.x + 100, header.y + header.h * 0.5);
  t.ui.pointerDown(header.x + 100, header.y + header.h * 0.5);
  t.ui.pointerUp(header.x + 100, header.y + header.h * 0.5);
  R1_EXPECT(!section.collapsed());
  return r1test::finish();
}
