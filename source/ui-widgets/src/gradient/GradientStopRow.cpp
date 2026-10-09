// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GradientStopRow.h and its private swatch widget.
// Invariants: the row never edits the gradient itself; it reports parsed values with a begin / end
//   bracket and the editor refreshes the fields; callbacks may destroy the row.
// Callers: the gradient editor; tests.
#include "r1ui/widgets/gradient/GradientStopRow.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/colorpicker/NumberText.h"
#include "r1ui/widgets/colorpicker/PickerDraw.h"
#include "r1ui/widgets/colorpicker/PickerEntry.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

namespace events = core::events;
using theme::State::kNone;
using theme::State::kSelected;
using theme::StyleProperty;
using theme::StyleRuleEntry;

constexpr double kRowHeight = 30.0;
constexpr double kSwatch = 16.0;

const StyleRuleEntry kRows[] = {
    {"gradient.stoprow", kNone, StyleProperty::Background, "transparent"},
    {"gradient.stoprow", kSelected, StyleProperty::Background, "color:hover@0.5"},
    {"gradient.stoprow", kNone, StyleProperty::Radius, "radius:panel"},
};

// The colour square of a row (checkerboard under the colour, 14 px inside a 16 px box, radius 3).
class StopSwatch final : public WidgetObject {
 public:
  const char* typeName() const override { return "StopSwatch"; }
  void onAttached() override {
    style().width = core::layout::Length::px(kSwatch);
    style().height = core::layout::Length::px(kSwatch);
    style().flexShrink = 0.0;
    node().flags.hitTestTransparent = true;
  }
  void setColour(const color::Rgba& c) {
    if (c == colour_) return;
    colour_ = c;
    requestPaint();
  }
  void paint(PaintContext& ctx) override {
    // The 16 px square has a 1 px transparent border: the colour fills the 14 px inside it.
    const render::Rect outer = ctx.box();
    const float inset = ctx.px(1.0);
    const render::Rect box{outer.x + inset, outer.y + inset, outer.w - 2 * inset, outer.h - 2 * inset};
    const float radius = ctx.px(3.0);
    pickerdraw::drawCheckerboard(ctx, box, radius, 4.0, ctx.color("checkerboard"), ctx.color("checkerboard-muted"));
    ctx.painter().fillRoundedRect(box, render::CornerRadii::uniform(radius),
                                  {static_cast<float>(colour_.rgb.r), static_cast<float>(colour_.rgb.g), static_cast<float>(colour_.rgb.b),
                                   static_cast<float>(colour_.a)});
  }

 private:
  color::Rgba colour_;
};

}  // namespace

std::span<const StyleRuleEntry> GradientStopRow::styleRows() { return kRows; }

void GradientStopRow::onAttached() {
  core::layout::Style& s = style();
  s.direction = core::layout::FlexDirection::Row;
  s.alignItems = core::layout::Align::Center;
  s.height = core::layout::Length::px(kRowHeight);
  s.gapColumn = 4.0;
  s.padding[core::layout::kTop] = s.padding[core::layout::kBottom] = 2.0;
  s.flexShrink = 0.0;
  setFocusable(true);

  PickerEntry& pos = ui().create<PickerEntry>(id(), PickerEntry::Look::Field);
  position_ = pos.id();
  pos.style().width = core::layout::Length::px(62);
  pos.style().flexShrink = 0.0;
  pos.setSuffix("%");
  pos.setFontSize(13.0);
  pos.setPadLeft(10.0);
  pos.setAccessibleName("Stop position");
  pos.onCommit = [this](std::string_view t) { return commitPosition(t); };
  pos.onFocused = [this] {
    if (onSelect) onSelect(stopId_);
  };

  StopSwatch& swatch = ui().create<StopSwatch>(id());
  swatch_ = swatch.id();

  PickerEntry& hex = ui().create<PickerEntry>(id(), PickerEntry::Look::Input);
  hex_ = hex.id();
  hex.style().width = core::layout::Length::px(70);
  hex.style().height = core::layout::Length::px(22);
  hex.style().flexShrink = 0.0;
  hex.setFontSize(11.0);
  hex.setMaxBytes(32);
  hex.setAccessibleName("Stop colour");
  hex.onCommit = [this](std::string_view t) { return commitHex(t); };
  hex.onFocused = [this] {
    if (onSelect) onSelect(stopId_);
  };

  PickerEntry& opacity = ui().create<PickerEntry>(id(), PickerEntry::Look::Field);
  opacity_ = opacity.id();
  opacity.style().width = core::layout::Length::px(62);
  opacity.style().flexShrink = 0.0;
  opacity.setSuffix("%");
  opacity.setFontSize(13.0);
  opacity.setPadLeft(10.0);
  opacity.setAccessibleName("Stop opacity");
  opacity.onCommit = [this](std::string_view t) { return commitOpacity(t); };
  opacity.onFocused = [this] {
    if (onSelect) onSelect(stopId_);
  };
}

PickerEntry& GradientStopRow::positionEntry() const { return *ui().objectAs<PickerEntry>(position_); }
PickerEntry& GradientStopRow::hexEntry() const { return *ui().objectAs<PickerEntry>(hex_); }
PickerEntry& GradientStopRow::opacityEntry() const { return *ui().objectAs<PickerEntry>(opacity_); }

std::string_view GradientStopRow::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view("Gradient stop") : WidgetObject::accessibleName();
}

void GradientStopRow::setStop(const gradient::Stop& stop, bool selected) {
  stop_ = stop;
  setSelected(selected);
  positionEntry().setText(formatNumber(std::round(stop.position * 100.0), 0));
  hexEntry().setText(color::formatHex(stop.color));
  opacityEntry().setText(formatNumber(std::round(stop.color.a * 100.0), 0));
  if (StopSwatch* s = ui().objectAs<StopSwatch>(swatch_)) s->setColour(stop.color);
}

void GradientStopRow::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style("gradient.stoprow");
  if (rs.background.a > 0) ctx.painter().fillRoundedRect(ctx.box(), render::CornerRadii::uniform(ctx.px(rs.radius)), ctx.color(rs.background));
}

void GradientStopRow::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.box(), ctx.px(4.0));
}

void GradientStopRow::onPointerDown(Event& e) {
  // The capture phase sees presses on the fields too; selecting never consumes the press.
  if (e.button != events::Button::Left || e.phase == events::Phase::Bubble) return;
  if (onSelect) onSelect(stopId_);
}

void GradientStopRow::onKeyDown(Event& e) {
  if ((e.key == events::Key::Delete || e.key == events::Key::Backspace) && e.target == id()) {
    e.markHandled();
    if (onRemove) onRemove(stopId_);
  }
}

bool GradientStopRow::commitPosition(std::string_view text) {
  const auto v = parseNumber(text);
  if (!v) return false;
  runEdit([&] {
    if (onPosition) onPosition(stopId_, color::clamp01(*v / 100.0));
  });
  return true;
}

bool GradientStopRow::commitHex(std::string_view text) {
  const auto parsed = color::parseHex(text);
  if (!parsed) return false;
  std::string_view digits = text;
  while (!digits.empty() && (digits.front() == ' ' || digits.front() == '#')) digits.remove_prefix(1);
  while (!digits.empty() && digits.back() == ' ') digits.remove_suffix(1);
  const bool hasAlpha = digits.size() == 4 || digits.size() == 8;
  const color::Rgba colour{parsed->rgb, hasAlpha ? parsed->a : stop_.color.a};
  runEdit([&] {
    if (onColour) onColour(stopId_, colour);
  });
  return true;
}

bool GradientStopRow::commitOpacity(std::string_view text) {
  const auto v = parseNumber(text);
  if (!v) return false;
  const color::Rgba colour{stop_.color.rgb, color::clamp01(*v / 100.0)};
  runEdit([&] {
    if (onColour) onColour(stopId_, colour);
  });
  return true;
}

void GradientStopRow::runEdit(const std::function<void()>& edit) {
  // An edit can re-sort the stops, which rebuilds the rows and destroys this one while it is still on
  // the stack: the bracket is closed through copies of the callbacks, never through `this`.
  const auto begin = onBegin;
  const auto end = onEnd;
  if (begin) begin();
  edit();
  if (end) end();
}

}  // namespace r1ui::widgets
