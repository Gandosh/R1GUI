// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: behaviour oracle for ColorPicker and its parts: measured geometry, the begin / change / end
//   contract of every gesture (pointer drag on the square and sliders, keyboard nudges with Shift,
//   typed values in the RGB / HSL / HEX entries), Escape and capture loss restoring the start
//   colour, hue memory of greys, mode tabs, the component select, swatches (add, apply, remove,
//   bound), eyedropper and save slots, disabled behaviour and hostile input (NaN colours, huge
//   swatch lists, widgets destroyed inside callbacks).
// Callers: CTest (colorpicker, fast tier, no GPU; paint is exercised on the recording Painter).
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/render/Painter.h"
#include "r1ui/widgets/colorpicker/ColorPicker.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::events::Key;
namespace events = r1ui::core::events;
namespace layout = r1ui::core::layout;

struct Recorder {
  int begins = 0;
  int ends = 0;
  std::vector<ColorChange> changes;
  std::vector<int> openAtChange;  // gestureOpen() at each change
};

struct Fixture {
  r1test::TestUi t{400, 700};
  ColorPicker* picker = nullptr;
  Recorder rec;
  Fixture() {
    t.ui.rootStyle().alignItems = layout::Align::Start;
    picker = &t.ui.create<ColorPicker>(t.ui.root());
    picker->setColor({{212.0 / 255, 212.0 / 255, 212.0 / 255}, 1.0});
    picker->onBeginInteraction = [this] { ++rec.begins; };
    picker->onEndInteraction = [this] { ++rec.ends; };
    picker->onChanged = [this](const ColorChange& c) {
      rec.changes.push_back(c);
      rec.openAtChange.push_back(picker->gestureOpen() ? 1 : 0);
    };
    t.layout();
  }
  layout::Rect rect(r1ui::core::tree::WidgetId id) { return t.ui.absRect(id); }
  void clear() { rec = {}; }
  bool balanced() const { return rec.begins == rec.ends && !picker->gestureOpen(); }
};

void paintOnce(r1test::TestUi& t) {
  r1ui::render::Painter painter;
  painter.begin(static_cast<uint32_t>(t.ui.viewportWidth() * t.ui.scale()), static_cast<uint32_t>(t.ui.viewportHeight() * t.ui.scale()));
  t.ui.paint(painter);
  t.ui.finishPaint();
  painter.end();
}

void typeInto(Fixture& f, PickerEntry& e, std::string_view text) {
  f.t.ui.router().focus(e.id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(static_cast<Key>('A'), events::Mod::kCtrl);  // the entry may already have been focused
  for (char c : text) f.t.ui.textInput(static_cast<char32_t>(c));
  f.t.ui.keyDown(Key::Enter);
}

void testGeometry() {
  Fixture f;
  const layout::Rect root = f.rect(f.picker->id());
  R1_EXPECT(root.w == 240);  // 222 + 2 * (8 padding + 1 border)
  const layout::Rect sv = f.rect(f.picker->square().id());
  R1_EXPECT(sv.w == 222 && sv.h == 140 && sv.x == root.x + 9);
  const layout::Rect hue = f.rect(f.picker->hueRow().id());
  const layout::Rect alpha = f.rect(f.picker->alphaRow().id());
  R1_EXPECT(hue.w == 222 && hue.h == 26 && alpha.h == 26 && alpha.y == hue.y + 26 + 8);
  const layout::Rect track = f.rect(f.picker->hueRow().track().id());
  R1_EXPECT(track.x == hue.x + 24 && track.h == 14 && track.x + track.w + 8 + 96 == hue.x + hue.w);
  const layout::Rect field = f.rect(f.picker->hueRow().entry().id());
  R1_EXPECT(field.w == 96 && field.h == 26);
  const layout::Rect select = f.rect(f.picker->modeSelect().id());
  const layout::Rect pipette = f.rect(f.picker->eyedropper().id());
  R1_EXPECT(select.h == 26 && pipette.w == 26 && pipette.h == 26 && pipette.x == select.x + select.w + 6);
  const layout::Rect channels = f.rect(f.picker->channels().id());
  R1_EXPECT(channels.h == 26 && channels.w == 222);
  const layout::Rect tab = f.rect(f.picker->modeTab(PickerMode::Solid).id());
  const layout::Rect tab2 = f.rect(f.picker->modeTab(PickerMode::Gradient).id());
  R1_EXPECT(tab.w == 24 && tab.h == 24 && tab2.x == tab.x + 26);
  // Without chrome and tabs the picker is a plain column.
  f.picker->setChrome(false);
  f.picker->setShowModeTabs(false);
  f.picker->style().width = layout::Length::px(222);
  f.picker->requestLayout();
  f.t.layout();
  R1_EXPECT(f.rect(f.picker->square().id()).y == f.rect(f.picker->id()).y);
  paintOnce(f.t);
}

void testSetColorIsSilent() {
  Fixture f;
  f.picker->setColor({{1, 0, 0}, 0.5});
  R1_EXPECT(f.rec.begins == 0 && f.rec.ends == 0 && f.rec.changes.empty());
  R1_EXPECT(f.picker->color().a == 0.5 && f.picker->state().hsv.h == 0 && f.picker->state().hsv.s == 1);
  f.picker->setColor({{0.5, 0.5, 0.5}, 1.0});
  f.picker->setColor({{0.5, 0.5, 0.5}, 1.0});
  R1_EXPECT(f.picker->state().hsv.s == 0 && f.rec.changes.empty());
  // Hue memory: a grey keeps the hue the user had.
  f.picker->setColor({{0, 0, 1}, 1.0});
  const double blueHue = f.picker->state().hsv.h;
  f.picker->setColor({{0.3, 0.3, 0.3}, 1.0});
  R1_EXPECT(f.picker->state().hsv.h == blueHue && f.picker->hueRow().track().position() == blueHue / 360.0);
  // Hostile colours never reach the model unclamped.
  const double nan = std::numeric_limits<double>::quiet_NaN();
  f.picker->setColor({{nan, 5, -3}, nan});
  const auto c = f.picker->color();
  R1_EXPECT(std::isfinite(c.rgb.r) && c.rgb.g == 1 && c.rgb.b == 0 && c.a == 0);
  paintOnce(f.t);
}

void testSquareDrag() {
  Fixture f;
  SvSquare& sv = f.picker->square();
  const layout::Rect r = f.rect(sv.id());
  const double x = r.x + r.w * 0.25;
  const double y = r.y + r.h * 0.5;
  f.t.ui.pointerMove(x, y);
  f.t.ui.pointerDown(x, y);
  R1_EXPECT(f.rec.begins == 1 && f.rec.ends == 0 && f.picker->gestureOpen());
  R1_EXPECT(f.rec.changes.size() == 1 && f.rec.changes.back().interactive);
  R1_EXPECT(std::fabs(f.picker->state().hsv.s - 0.25) < 0.01 && std::fabs(f.picker->state().hsv.v - 0.5) < 0.01);
  f.t.ui.pointerMove(r.x + r.w * 0.75, r.y + r.h * 0.25);
  f.t.ui.pointerMove(r.x + r.w + 50, r.y - 50);  // outside: clamped to the corner
  R1_EXPECT(f.picker->state().hsv.s == 1.0 && f.picker->state().hsv.v == 1.0);
  f.t.ui.pointerUp(r.x + r.w + 50, r.y - 50);
  R1_EXPECT(f.rec.begins == 1 && f.rec.ends == 1 && f.rec.changes.size() == 3);
  R1_EXPECT(!f.rec.changes.back().interactive == false);  // the last change is still inside the gesture
  R1_EXPECT(f.balanced());
  // The colour follows the model: pure red at the top right corner.
  R1_EXPECT(f.picker->color().rgb.r == 1.0 && f.picker->color().rgb.g == 0.0);
  // The hue is untouched by the square.
  R1_EXPECT(f.picker->state().hsv.h == 0.0);
}

void testEscapeAndCaptureLoss() {
  Fixture f;
  const layout::Rect r = f.rect(f.picker->square().id());
  const color::Rgba start = f.picker->color();
  f.t.ui.pointerMove(r.x + 10, r.y + 10);
  f.t.ui.pointerDown(r.x + 10, r.y + 10);
  f.t.ui.pointerMove(r.x + 100, r.y + 100);
  R1_EXPECT(!(f.picker->color() == start));
  R1_EXPECT(f.t.ui.keyDown(Key::Escape));  // cancels the drag and restores
  R1_EXPECT(f.picker->color() == start && f.balanced());
  f.t.ui.pointerUp(r.x + 100, r.y + 100);  // the late release is ignored
  R1_EXPECT(f.balanced() && f.picker->color() == start);

  // Capture lost (the window was deactivated): same restore, one bracket.
  f.clear();
  f.t.ui.pointerMove(r.x + 10, r.y + 10);
  f.t.ui.pointerDown(r.x + 10, r.y + 10);
  f.t.ui.pointerMove(r.x + 80, r.y + 90);
  f.t.ui.router().cancelPointerInteraction();
  R1_EXPECT(f.picker->color() == start && f.rec.begins == 1 && f.rec.ends == 1);
}

void testKeyboardNudges() {
  Fixture f;
  f.picker->setColor({{1, 0, 0}, 1});
  SvSquare& sv = f.picker->square();
  f.t.ui.router().focus(sv.id(), events::FocusReason::Keyboard);
  R1_EXPECT(sv.focusVisible());
  f.t.ui.keyDown(Key::Left);
  R1_EXPECT(std::fabs(f.picker->state().hsv.s - 0.99) < 1e-9 && f.rec.begins == 1 && f.rec.ends == 1 && f.rec.changes.size() == 1);
  f.t.ui.keyDown(Key::Down, events::Mod::kShift);
  R1_EXPECT(std::fabs(f.picker->state().hsv.v - 0.9) < 1e-9);
  f.t.ui.keyDown(Key::Right, events::Mod::kShift);
  R1_EXPECT(f.picker->state().hsv.s == 1.0);  // clamped
  const size_t before = f.rec.changes.size();
  f.t.ui.keyDown(Key::Right);  // already at the limit: no change, no bracket
  R1_EXPECT(f.rec.changes.size() == before && f.balanced());
  f.t.ui.keyDown(Key::PageUp);
  R1_EXPECT(f.picker->state().hsv.v == 1.0);

  // Hue track: arrows are degrees, Shift is ten, Home / End jump.
  ColorSliderTrack& hue = f.picker->hueRow().track();
  f.t.ui.router().focus(hue.id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(Key::Right);
  R1_EXPECT(std::fabs(f.picker->state().hsv.h - 1.0) < 1e-6);
  f.t.ui.keyDown(Key::Right, events::Mod::kShift);
  R1_EXPECT(std::fabs(f.picker->state().hsv.h - 11.0) < 1e-6);
  f.t.ui.keyDown(Key::Home);
  R1_EXPECT(f.picker->state().hsv.h == 0.0);
  f.t.ui.keyDown(Key::Left);  // already at the start: the track does not wrap
  R1_EXPECT(f.picker->state().hsv.h == 0.0);
  // Alpha track: percent.
  ColorSliderTrack& alpha = f.picker->alphaRow().track();
  f.t.ui.router().focus(alpha.id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(Key::Left);
  R1_EXPECT(std::fabs(f.picker->state().alpha - 0.99) < 1e-9);
  f.t.ui.keyDown(Key::Left, events::Mod::kShift);
  R1_EXPECT(std::fabs(f.picker->state().alpha - 0.89) < 1e-9);
  f.t.ui.keyDown(Key::End);
  R1_EXPECT(f.picker->state().alpha == 1.0);
  R1_EXPECT(f.balanced());
}

void testSliderDrag() {
  Fixture f;
  f.picker->setColor({{1, 0, 0}, 1});
  const layout::Rect tr = f.rect(f.picker->alphaRow().track().id());
  const double y = tr.y + tr.h / 2.0;
  f.t.ui.pointerMove(tr.x + 40, y);
  f.t.ui.pointerDown(tr.x + 7, y);  // the thumb centre can reach 7 px from the left end
  R1_EXPECT(f.picker->state().alpha == 0.0);
  f.t.ui.pointerMove(tr.x + tr.w * 0.5, y);
  R1_EXPECT(std::fabs(f.picker->state().alpha - 0.5) < 0.01);
  f.t.ui.pointerMove(tr.x + tr.w + 100, y);
  R1_EXPECT(f.picker->state().alpha == 1.0);
  f.t.ui.pointerUp(tr.x + tr.w + 100, y);
  R1_EXPECT(f.balanced() && f.rec.begins == 1);
  // The thumb of the hue track at the far right is hue 360 -> wraps to 0 in the model.
  const layout::Rect hr = f.rect(f.picker->hueRow().track().id());
  f.t.ui.pointerMove(hr.x + hr.w - 7, hr.y + 7);
  f.t.ui.pointerDown(hr.x + hr.w - 7, hr.y + 7);
  f.t.ui.pointerUp(hr.x + hr.w - 7, hr.y + 7);
  R1_EXPECT(f.picker->state().hsv.h < 360.0 && f.picker->state().hsv.h >= 0.0);
  // Right and middle buttons do nothing.
  f.clear();
  f.t.ui.pointerDown(hr.x + 20, hr.y + 7, events::Button::Right);
  f.t.ui.pointerUp(hr.x + 20, hr.y + 7, events::Button::Right);
  R1_EXPECT(f.rec.begins == 0);
}

void testEntries() {
  Fixture f;
  ChannelFields& ch = f.picker->channels();
  R1_EXPECT(ch.cell(0).text() == "212" && ch.cell(2).text() == "212");
  typeInto(f, ch.cell(0), "128");
  R1_EXPECT(f.picker->color().rgb.r == 128.0 / 255 && f.rec.begins == 1 && f.rec.ends == 1 && f.rec.changes.size() == 1);
  R1_EXPECT(ch.cell(0).text() == "128" && !ch.cell(0).dirty());
  typeInto(f, ch.cell(1), "999");  // clamped
  R1_EXPECT(f.picker->color().rgb.g == 1.0 && ch.cell(1).text() == "255");
  typeInto(f, ch.cell(2), "abc");  // rejected: reverts, no change
  R1_EXPECT(ch.cell(2).text() == "212" && f.rec.changes.size() == 2);
  typeInto(f, ch.cell(2), "-5");
  R1_EXPECT(std::fabs(f.picker->color().rgb.b) < 1e-12);
  // Arrow keys step the focused cell.
  f.t.ui.router().focus(ch.cell(0).id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(Key::Up, events::Mod::kShift);
  R1_EXPECT(ch.cell(0).text() == "138");

  // Hue and alpha entries.
  typeInto(f, f.picker->hueRow().entry(), "200");
  R1_EXPECT(std::fabs(f.picker->state().hsv.h - 200.0) < 1e-9);
  typeInto(f, f.picker->hueRow().entry(), "-30");
  R1_EXPECT(std::fabs(f.picker->state().hsv.h - 330.0) < 1e-9);
  typeInto(f, f.picker->hueRow().entry(), "720");
  R1_EXPECT(std::fabs(f.picker->state().hsv.h) < 1e-9);
  typeInto(f, f.picker->alphaRow().entry(), "25");
  R1_EXPECT(std::fabs(f.picker->state().alpha - 0.25) < 1e-9);
  typeInto(f, f.picker->alphaRow().entry(), "250");
  R1_EXPECT(f.picker->state().alpha == 1.0);
  typeInto(f, f.picker->alphaRow().entry(), "nan");
  R1_EXPECT(f.picker->state().alpha == 1.0 && f.picker->alphaRow().entry().text() == "100");
  R1_EXPECT(f.balanced());
}

void testComponentModes() {
  Fixture f;
  f.picker->setColor({{1, 0, 0}, 1});
  ChannelFields& ch = f.picker->channels();
  // Mode select with the keyboard: open, move, choose.
  PickerDropdown& select = f.picker->modeSelect();
  R1_EXPECT(select.selected() == 0 && select.items().size() == 3);
  f.t.ui.router().focus(select.id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(Key::Enter);
  R1_EXPECT(select.isOpen() && f.t.ui.overlays().any());
  f.t.layout();
  f.t.ui.keyDown(Key::Down);
  f.t.ui.keyDown(Key::Enter);
  R1_EXPECT(!select.isOpen() && f.picker->componentMode() == ComponentMode::Hsl && select.selected() == 1);
  R1_EXPECT(ch.cell(0).text() == "0" && ch.cell(1).text() == "100" && ch.cell(2).text() == "50");
  typeInto(f, ch.cell(0), "120");
  R1_EXPECT(f.picker->color().rgb.g == 1.0 && f.picker->color().rgb.r == 0.0);
  typeInto(f, ch.cell(2), "25");
  R1_EXPECT(std::fabs(color::toHsl(f.picker->color().rgb).l - 0.25) < 0.01);
  // The hue of a grey can still be edited in HSL.
  f.picker->setColor({{0.5, 0.5, 0.5}, 1});
  typeInto(f, ch.cell(0), "210");
  R1_EXPECT(f.picker->state().hsv.h == 210.0 && f.picker->color().rgb.r == 0.5);

  // HEX.
  f.picker->setComponentMode(ComponentMode::Hex);
  R1_EXPECT(ch.visibleCells() == 1 && ch.cell(0).text() == "808080");
  f.clear();
  typeInto(f, ch.cell(0), "#f00");
  R1_EXPECT(f.picker->color().rgb.r == 1.0 && f.picker->color().rgb.g == 0.0 && f.picker->state().alpha == 1.0);
  typeInto(f, ch.cell(0), "00ff0080");
  R1_EXPECT(f.picker->color().rgb.g == 1.0 && std::fabs(f.picker->state().alpha - 128.0 / 255) < 1e-9);
  typeInto(f, ch.cell(0), "12345");  // invalid length
  R1_EXPECT(ch.cell(0).text() == "00FF00" && f.picker->color().rgb.g == 1.0);
  typeInto(f, ch.cell(0), "zzzzzz");
  R1_EXPECT(ch.cell(0).text() == "00FF00");
  typeInto(f, ch.cell(0), std::string(500, 'f'));
  R1_EXPECT(ch.cell(0).text() == "00FF00");
  R1_EXPECT(f.balanced());
  paintOnce(f.t);
}

void testModeTabsAndSlots() {
  Fixture f;
  std::vector<PickerMode> modes;
  int eyedropper = 0;
  int save = 0;
  f.picker->onModeChanged = [&](PickerMode m) { modes.push_back(m); };
  f.picker->onEyedropper = [&] { ++eyedropper; };
  f.picker->onSaveSwatches = [&] { ++save; };
  const layout::Rect g = f.rect(f.picker->modeTab(PickerMode::Gradient).id());
  f.t.ui.pointerMove(g.x + 5, g.y + 5);
  f.t.ui.pointerDown(g.x + 5, g.y + 5);
  f.t.ui.pointerUp(g.x + 5, g.y + 5);
  R1_EXPECT(modes.size() == 1 && modes[0] == PickerMode::Gradient && f.picker->mode() == PickerMode::Gradient);
  R1_EXPECT(f.picker->modeTab(PickerMode::Gradient).active() && !f.picker->modeTab(PickerMode::Solid).active());
  f.t.ui.pointerDown(g.x + 5, g.y + 5);  // the active tab again: nothing
  f.t.ui.pointerUp(g.x + 5, g.y + 5);
  R1_EXPECT(modes.size() == 1);
  f.picker->setMode(PickerMode::Image);  // programmatic: no callback
  R1_EXPECT(modes.size() == 1 && f.picker->modeTab(PickerMode::Image).active());
  // Keyboard activation of a tab and of the slots.
  f.t.ui.router().focus(f.picker->modeTab(PickerMode::Solid).id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(Key::Space);
  R1_EXPECT(modes.size() == 2 && modes[1] == PickerMode::Solid);
  f.t.ui.router().focus(f.picker->eyedropper().id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(Key::Enter);
  R1_EXPECT(eyedropper == 1);
  f.t.ui.router().focus(f.picker->swatchRow().saveButton().id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(Key::Enter);
  R1_EXPECT(save == 1);
  // A disabled button gets no events.
  f.picker->eyedropper().setEnabled(false);
  f.t.layout();
  const layout::Rect e = f.rect(f.picker->eyedropper().id());
  f.t.ui.pointerMove(e.x + 5, e.y + 5);
  f.t.ui.pointerDown(e.x + 5, e.y + 5);
  f.t.ui.pointerUp(e.x + 5, e.y + 5);
  R1_EXPECT(eyedropper == 1);
  R1_EXPECT(f.picker->eyedropper().tooltipText() == "Eyedropper");
}

void testSwatches() {
  Fixture f;
  std::vector<std::vector<color::Rgba>> lists;
  f.picker->onSwatchesChanged = [&](const std::vector<color::Rgba>& l) { lists.push_back(l); };
  SwatchRow& row = f.picker->swatchRow();
  f.picker->setColor({{1, 0, 0}, 1});
  R1_EXPECT(row.addCurrent() && row.swatches().size() == 1 && lists.size() == 1);
  R1_EXPECT(!row.addCurrent() && lists.size() == 1);  // duplicates are not added
  f.picker->setColor({{0, 1, 0}, 0.5});
  f.t.ui.router().focus(row.addButton().id(), events::FocusReason::Keyboard);
  f.t.ui.keyDown(Key::Enter);
  R1_EXPECT(row.swatches().size() == 2 && lists.back().size() == 2);
  f.t.layout();
  // Click the first swatch: applies it as one gesture.
  r1ui::core::tree::WidgetId first;
  f.t.ui.tree().forEachChild(f.t.ui.tree().lastChild(row.id()), [&](r1ui::core::tree::WidgetId c) {
    if (!first.valid()) first = c;
  });
  R1_EXPECT(first.valid());
  const layout::Rect s = f.rect(first);
  R1_EXPECT(s.w == 20 && s.h == 20);
  f.clear();
  f.t.ui.pointerMove(s.x + 5, s.y + 5);
  f.t.ui.pointerDown(s.x + 5, s.y + 5);
  f.t.ui.pointerUp(s.x + 5, s.y + 5);
  R1_EXPECT(f.picker->color().rgb.r == 1.0 && f.rec.begins == 1 && f.rec.ends == 1 && f.rec.changes.size() == 1);
  // Delete removes the focused swatch.
  f.t.ui.router().focus(first, events::FocusReason::Keyboard);
  f.t.ui.keyDown(Key::Delete);
  R1_EXPECT(row.swatches().size() == 1 && lists.back().size() == 1 && row.swatches()[0].rgb.g == 1.0);
  // Alt-click removes too.
  f.t.layout();
  r1ui::core::tree::WidgetId only;
  f.t.ui.tree().forEachChild(f.t.ui.tree().lastChild(row.id()), [&](r1ui::core::tree::WidgetId c) { only = c; });
  const layout::Rect o = f.rect(only);
  f.t.ui.pointerMove(o.x + 5, o.y + 5);
  f.t.ui.pointerDown(o.x + 5, o.y + 5, events::Button::Left, events::Mod::kAlt);
  f.t.ui.pointerUp(o.x + 5, o.y + 5, events::Button::Left, events::Mod::kAlt);
  R1_EXPECT(row.swatches().empty());
  R1_EXPECT(!row.removeAt(0) && !row.removeAt(1'000'000));
  // A list of a million entries is cut to the bound; hostile colours are clamped.
  std::vector<color::Rgba> huge(1'000'000, color::Rgba{{std::numeric_limits<double>::quiet_NaN(), 2, -1}, 7});
  row.setSwatches(std::move(huge));
  R1_EXPECT(row.swatches().size() == SwatchRow::kMaxSwatches && row.swatches()[0].rgb.g == 1.0 && row.swatches()[0].a == 1.0);
  f.t.layout();
  paintOnce(f.t);
  R1_EXPECT(!row.addCurrent());  // full
  row.setSwatches({});
  R1_EXPECT(row.swatches().empty());
}

void testDestroyInsideCallbacks() {
  {
    Fixture f;
    f.picker->onBeginInteraction = [&] { f.t.ui.destroy(f.picker->id()); };
    const layout::Rect r = f.rect(f.picker->square().id());
    f.t.ui.pointerMove(r.x + 10, r.y + 10);
    f.t.ui.pointerDown(r.x + 10, r.y + 10);
    f.t.ui.pointerMove(r.x + 20, r.y + 20);
    f.t.ui.pointerUp(r.x + 20, r.y + 20);
    f.t.layout();
    paintOnce(f.t);
  }
  {
    Fixture f;
    f.picker->onChanged = [&](const ColorChange&) { f.t.ui.destroy(f.picker->id()); };
    f.t.ui.router().focus(f.picker->channels().cell(0).id(), events::FocusReason::Keyboard);
    f.t.ui.textInput('5');
    f.t.ui.keyDown(Key::Enter);
    f.t.layout();
    paintOnce(f.t);
  }
  {
    Fixture f;
    f.picker->onEndInteraction = [&] { f.t.ui.destroy(f.picker->id()); };
    f.t.ui.router().focus(f.picker->square().id(), events::FocusReason::Keyboard);
    f.t.ui.keyDown(Key::Left);
    f.t.layout();
    paintOnce(f.t);
  }
  {
    // Destroying the picker with the mode list open closes the overlay.
    Fixture f;
    f.picker->modeSelect().open();
    R1_EXPECT(f.t.ui.overlays().any());
    f.t.ui.destroy(f.picker->id());
    f.t.layout();
    R1_EXPECT(!f.t.ui.overlays().any());
  }
}

void testScalesAndDisabled() {
  for (const float scale : {1.0f, 1.25f, 2.0f}) {
    r1test::TestUi t(500, 800, scale);
    t.ui.rootStyle().alignItems = layout::Align::Start;
    ColorPicker& p = t.ui.create<ColorPicker>(t.ui.root());
    p.setSwatches({{{1, 0, 0}, 1}, {{0, 0, 1}, 0.3}});
    t.layout();
    paintOnce(t);
    R1_EXPECT(t.ui.absRect(p.id()).w == 240);
  }
  Fixture f;
  f.picker->setEnabled(false);
  f.t.layout();
  const layout::Rect r = f.rect(f.picker->square().id());
  f.t.ui.pointerMove(r.x + 10, r.y + 10);
  f.t.ui.pointerDown(r.x + 10, r.y + 10);
  f.t.ui.pointerUp(r.x + 10, r.y + 10);
  R1_EXPECT(f.rec.begins == 0);
  paintOnce(f.t);
}

}  // namespace

int main() {
  testGeometry();
  testSetColorIsSilent();
  testSquareDrag();
  testEscapeAndCaptureLoss();
  testKeyboardNudges();
  testSliderDrag();
  testEntries();
  testComponentModes();
  testModeTabsAndSlots();
  testSwatches();
  testDestroyInsideCallbacks();
  testScalesAndDisabled();
  return r1test::finish();
}
