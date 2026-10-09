// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ChannelFields.h.
// Invariants: the state is only replaced through apply(), which clamps through ColorState; every
//   cell shows the text of the current state after a commit (a rejected value reverts to it); a
//   callback that destroys the widget stops all further work.
// Callers: the colour picker; tests.
#include "r1ui/widgets/colorpicker/ChannelFields.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/colorpicker/NumberText.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using theme::State::kNone;
using theme::StyleProperty;
using theme::StyleRuleEntry;

const StyleRuleEntry kRows[] = {
    {"picker.channels", kNone, StyleProperty::Background, "color:input"},
    {"picker.channels", kNone, StyleProperty::BorderColor, "color:border"},
    {"picker.channels", kNone, StyleProperty::BorderWidth, "number:1"},
    {"picker.channels", kNone, StyleProperty::Radius, "radius:panel"},
};

}  // namespace

std::span<const StyleRuleEntry> ChannelFields::styleRows() { return kRows; }

void ChannelFields::onAttached() {
  core::layout::Style& s = style();
  s.direction = core::layout::FlexDirection::Row;
  s.alignItems = core::layout::Align::Stretch;
  s.height = core::layout::Length::px(ui().services().tokens().space("control").value_or(26.0));
  s.flexShrink = 0.0;
  for (double& p : s.padding) p = 1.0;
  for (int i = 0; i < 3; ++i) {
    PickerEntry& e = ui().create<PickerEntry>(id(), PickerEntry::Look::Bare);
    cells_[static_cast<size_t>(i)] = e.id();
    e.style().flexGrow = 1.0;
    e.style().flexBasis = core::layout::Length::px(0);
    e.style().height = core::layout::Length::autoValue();
    e.onCommit = [this, i](std::string_view text) { return commit(i, text); };
    e.onStep = [this, i](int steps) { step(i, steps); };
  }
  setMode(ComponentMode::Rgb);
}

PickerEntry& ChannelFields::cell(int index) const { return *ui().objectAs<PickerEntry>(cells_[static_cast<size_t>(std::clamp(index, 0, 2))]); }

void ChannelFields::setMode(ComponentMode mode) {
  mode_ = mode;
  for (int i = 0; i < 3; ++i) {
    PickerEntry& e = cell(i);
    e.style().display = (mode == ComponentMode::Hex && i > 0) ? core::layout::Display::None : core::layout::Display::Flex;
    static const char* const kNames[3][3] = {{"Red", "Green", "Blue"}, {"Hue", "Saturation", "Lightness"}, {"Hex", "", ""}};
    e.setAccessibleName(kNames[static_cast<int>(mode)][i]);
    e.requestLayout();
  }
  refresh();
  requestLayout();
  requestPaint();
}

void ChannelFields::setColorState(const color::ColorState& state) {
  state_ = state;
  refresh();
}

void ChannelFields::refresh() {
  const color::Rgba c = state_.rgba();
  switch (mode_) {
    case ComponentMode::Rgb:
      cell(0).setText(std::to_string(color::to8(c.rgb.r)));
      cell(1).setText(std::to_string(color::to8(c.rgb.g)));
      cell(2).setText(std::to_string(color::to8(c.rgb.b)));
      break;
    case ComponentMode::Hsl: {
      color::Hsl hsl = color::toHsl(c.rgb);
      if (hsl.s == 0.0 || hsl.l == 0.0 || hsl.l == 1.0) hsl.h = state_.hsv.h;
      cell(0).setText(formatNumber(std::round(hsl.h), 0));
      cell(1).setText(formatNumber(std::round(hsl.s * 100.0), 0));
      cell(2).setText(formatNumber(std::round(hsl.l * 100.0), 0));
      break;
    }
    case ComponentMode::Hex: cell(0).setText(color::formatHex(c)); break;
  }
}

void ChannelFields::apply(const color::ColorState& next) {
  const core::tree::WidgetId self = id();
  if (onBegin) onBegin();
  if (!ui().alive(self)) return;
  state_ = next;
  refresh();
  if (onChange) onChange(state_);
  if (!ui().alive(self)) return;
  if (onEnd) onEnd();
}

bool ChannelFields::commit(int index, std::string_view text) {
  color::ColorState next = state_;
  if (mode_ == ComponentMode::Hex) {
    const auto parsed = color::parseHex(text);
    if (!parsed) return false;
    std::string_view digits = text;
    while (!digits.empty() && (digits.front() == ' ' || digits.front() == '#')) digits.remove_prefix(1);
    while (!digits.empty() && digits.back() == ' ') digits.remove_suffix(1);
    const bool hasAlpha = digits.size() == 4 || digits.size() == 8;
    next.adopt({parsed->rgb, hasAlpha ? parsed->a : state_.alpha});
    apply(next);
    return true;
  }
  const auto value = parseNumber(text);
  if (!value) return false;
  const color::Rgba current = state_.rgba();
  if (mode_ == ComponentMode::Rgb) {
    color::Rgb rgb{color::from8(color::to8(current.rgb.r)), color::from8(color::to8(current.rgb.g)), color::from8(color::to8(current.rgb.b))};
    const double channel = color::from8(static_cast<int>(std::lround(std::clamp(*value, 0.0, 255.0))));
    (index == 0 ? rgb.r : index == 1 ? rgb.g : rgb.b) = channel;
    next.adopt({rgb, current.a});
  } else {
    color::Hsl hsl = color::toHsl(current.rgb);
    if (hsl.s == 0.0 || hsl.l == 0.0 || hsl.l == 1.0) hsl.h = state_.hsv.h;
    if (index == 0) hsl.h = color::wrapHue(*value);
    else if (index == 1) hsl.s = color::clamp01(*value / 100.0);
    else hsl.l = color::clamp01(*value / 100.0);
    next.adopt({color::toRgb(hsl), current.a});
    if (index == 0 && (next.hsv.s == 0.0 || next.hsv.v == 0.0)) next.setHue(hsl.h);
  }
  apply(next);
  return true;
}

void ChannelFields::step(int index, int steps) {
  if (mode_ == ComponentMode::Hex) return;
  const auto current = parseNumber(cell(index).text());
  if (!current) return;
  commit(index, formatNumber(*current + steps, 0));
}

void ChannelFields::paint(PaintContext& ctx) { ctx.fillBox(ctx.resolve("picker.channels", 0)); }

void ChannelFields::paintOver(PaintContext& ctx) {
  const render::Color line = ctx.color("border");
  for (int i = 0; i + 1 < visibleCells(); ++i) {
    const core::layout::Rect r = ui().absRect(cell(i).id());
    const render::Rect box = ctx.toPhysical(r.x + r.w, r.y, 1.0, r.h);
    ctx.painter().fillRect(box, line);
  }
}

}  // namespace r1ui::widgets
