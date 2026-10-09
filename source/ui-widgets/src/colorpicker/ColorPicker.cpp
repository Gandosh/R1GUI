// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ColorPicker.h.
// Invariants: state_ is the only copy of the colour; every part is a view refreshed from it after
//   each change (parts ignore a refresh that equals what they show, so a dragged part never fights
//   its own value); begin / end callbacks are balanced by gestureDepth_ however many parts take part
//   in one gesture; callbacks may destroy the picker, which stops all further work.
// Callers: application code, the gradient editor, tests.
#include "r1ui/widgets/colorpicker/ColorPicker.h"

#include <algorithm>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using theme::State::kNone;
using theme::StyleProperty;
using theme::StyleRuleEntry;

constexpr double kPad = 9.0;  // 8 px padding + the 1 px border (the border is not a layout box)
constexpr double kSectionGap = 8.0;

const StyleRuleEntry kRows[] = {
    {"picker.panel", kNone, StyleProperty::Background, "color:panel"},
    {"picker.panel", kNone, StyleProperty::BorderColor, "color:border"},
    {"picker.panel", kNone, StyleProperty::BorderWidth, "number:1"},
    {"picker.panel", kNone, StyleProperty::Radius, "radius:lg"},
};

// The widget object of a part by id; the parts are created in onAttached and live as long as the picker.
template <class T>
T& partOf(const WidgetObject& owner, core::tree::WidgetId id) {
  return *owner.ui().objectAs<T>(id);
}

}  // namespace

std::span<const StyleRuleEntry> ColorPicker::styleRows() { return kRows; }

std::string_view ColorPicker::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view("Color picker") : WidgetObject::accessibleName();
}

void ColorPicker::onAttached() {
  core::layout::Style& s = style();
  s.direction = core::layout::FlexDirection::Column;
  s.alignItems = core::layout::Align::Stretch;
  s.gapRow = kSectionGap;
  s.flexShrink = 0.0;
  state_ = color::ColorState::from({{0.0, 0.0, 0.0}, 1.0});

  // ---- mode strip ----
  PickerBox& tabs = ui().create<PickerBox>(id(), "PickerTabs");
  tabs_ = tabs.id();
  tabs.style().direction = core::layout::FlexDirection::Row;
  tabs.style().gapColumn = 2.0;
  tabs.style().flexShrink = 0.0;
  static const char* const kIcons[3] = {"square", "blend", "image"};
  static const char* const kNames[3] = {"Solid", "Gradient", "Image"};
  for (int i = 0; i < 3; ++i) {
    PickerButton& b = ui().create<PickerButton>(tabs.id(), kIcons[i], PickerButton::Look::Tab, 24.0, 14.0);
    tabButtons_[i] = b.id();
    b.setTooltipAndName(kNames[i]);
    b.onActivate = [this, i] {
      const PickerMode picked = static_cast<PickerMode>(i);
      if (picked == mode_) return;
      setMode(picked);
      if (onModeChanged) onModeChanged(picked);
    };
  }
  modeTab(PickerMode::Solid).setActive(true);

  // ---- saturation / value square ----
  SvSquare& sv = ui().create<SvSquare>(id());
  square_ = sv.id();
  sv.onBegin = [this] { beginGesture(); };
  sv.onChange = [this](double sat, double val) {
    color::ColorState next = state_;
    next.setSaturationValue(sat, val);
    commit(next);
  };
  sv.onEnd = [this] { endGesture(); };

  // ---- hue and alpha ----
  ColorSliderRow& hue = ui().create<ColorSliderRow>(id(), ColorSliderTrack::Kind::Hue);
  hue_ = hue.id();
  hue.onBegin = [this] { beginGesture(); };
  hue.onChange = [this](double degrees) {
    color::ColorState next = state_;
    next.setHue(degrees);
    commit(next);
  };
  hue.onEnd = [this] { endGesture(); };

  ColorSliderRow& alpha = ui().create<ColorSliderRow>(id(), ColorSliderTrack::Kind::Alpha);
  alpha_ = alpha.id();
  alpha.onBegin = [this] { beginGesture(); };
  alpha.onChange = [this](double a) {
    color::ColorState next = state_;
    next.setAlpha(a);
    commit(next);
  };
  alpha.onEnd = [this] { endGesture(); };

  // ---- component mode + eyedropper ----
  PickerBox& row = ui().create<PickerBox>(id(), "PickerModeRow");
  modeRow_ = row.id();
  row.style().direction = core::layout::FlexDirection::Row;
  row.style().gapColumn = 6.0;
  row.style().flexShrink = 0.0;
  PickerDropdown& select = ui().create<PickerDropdown>(row.id());
  modeSelect_ = select.id();
  select.style().flexGrow = 1.0;
  select.setItems({"RGB", "HSL", "HEX"});
  select.setSelected(0);
  select.setAccessibleName("Color mode");
  select.onSelect = [this](int index) { setComponentMode(static_cast<ComponentMode>(index)); };
  PickerButton& pipette = ui().create<PickerButton>(row.id(), "pipette", PickerButton::Look::Field, 26.0, 14.0);
  eyedropper_ = pipette.id();
  pipette.setTooltipAndName("Eyedropper");
  pipette.onActivate = [this] {
    if (onEyedropper) onEyedropper();
  };

  // ---- channel entries ----
  ChannelFields& fields = ui().create<ChannelFields>(id());
  channels_ = fields.id();
  fields.onBegin = [this] { beginGesture(); };
  fields.onChange = [this](const color::ColorState& next) { commit(next); };
  fields.onEnd = [this] { endGesture(); };

  // ---- swatches ----
  SwatchRow& sw = ui().create<SwatchRow>(id());
  swatches_ = sw.id();
  sw.onApply = [this](const color::Rgba& c) {
    beginGesture();
    color::ColorState next = state_;
    next.adopt(c);
    commit(next);
    endGesture();
  };
  sw.onSwatchesChanged = [this](const std::vector<color::Rgba>& list) {
    if (onSwatchesChanged) onSwatchesChanged(list);
  };
  sw.onSave = [this] {
    if (onSaveSwatches) onSaveSwatches();
  };

  setChrome(true);
  refreshParts();
}

SvSquare& ColorPicker::square() const { return partOf<SvSquare>(*this, square_); }
ColorSliderRow& ColorPicker::hueRow() const { return partOf<ColorSliderRow>(*this, hue_); }
ColorSliderRow& ColorPicker::alphaRow() const { return partOf<ColorSliderRow>(*this, alpha_); }
ChannelFields& ColorPicker::channels() const { return partOf<ChannelFields>(*this, channels_); }
PickerDropdown& ColorPicker::modeSelect() const { return partOf<PickerDropdown>(*this, modeSelect_); }
PickerButton& ColorPicker::eyedropper() const { return partOf<PickerButton>(*this, eyedropper_); }
SwatchRow& ColorPicker::swatchRow() const { return partOf<SwatchRow>(*this, swatches_); }
PickerButton& ColorPicker::modeTab(PickerMode mode) const { return partOf<PickerButton>(*this, tabButtons_[static_cast<int>(mode)]); }

void ColorPicker::setChrome(bool chrome) {
  chrome_ = chrome;
  core::layout::Style& s = style();
  for (double& p : s.padding) p = chrome ? kPad : 0.0;
  s.width = chrome ? core::layout::Length::px(kContentWidth + 2.0 * kPad) : core::layout::Length::autoValue();
  requestLayout();
  requestPaint();
}

void ColorPicker::setShowModeTabs(bool show) {
  if (WidgetObject* tabs = ui().object(tabs_)) {
    tabs->style().display = show ? core::layout::Display::Flex : core::layout::Display::None;
    tabs->requestLayout();
  }
}

void ColorPicker::setMode(PickerMode mode) {
  mode_ = mode;
  for (int i = 0; i < 3; ++i) partOf<PickerButton>(*this, tabButtons_[i]).setActive(i == static_cast<int>(mode));
}

void ColorPicker::setComponentMode(ComponentMode mode) {
  component_ = mode;
  modeSelect().setSelected(static_cast<int>(mode));
  channels().setMode(mode);
}

void ColorPicker::setSwatches(std::vector<color::Rgba> swatches) { swatchRow().setSwatches(std::move(swatches)); }
const std::vector<color::Rgba>& ColorPicker::swatches() const { return swatchRow().swatches(); }

void ColorPicker::setColor(const color::Rgba& c) {
  state_.adopt(c);
  refreshParts();
}

void ColorPicker::refreshParts() {
  const color::Rgb rgb = state_.rgb();
  square().setDisplay(state_.hsv.h, state_.hsv.s, state_.hsv.v, rgb);
  hueRow().setHue(state_.hsv.h);
  alphaRow().setAlpha(state_.alpha);
  alphaRow().setBase(rgb);
  channels().setColorState(state_);
  swatchRow().setCurrent(state_.rgba());
}

void ColorPicker::beginGesture() {
  if (gestureDepth_++ != 0) return;
  if (onBeginInteraction) onBeginInteraction();
}

void ColorPicker::endGesture() {
  if (gestureDepth_ <= 0) return;
  if (--gestureDepth_ != 0) return;
  if (onEndInteraction) onEndInteraction();
}

void ColorPicker::commit(const color::ColorState& next) {
  state_ = next;
  refreshParts();
  const core::tree::WidgetId self = id();
  if (onChanged) onChanged(ColorChange{state_.rgba(), gestureDepth_ > 0});
  if (!ui().alive(self)) return;
  requestPaint();
}

void ColorPicker::paint(PaintContext& ctx) {
  if (!chrome_) return;
  const render::Rect box = ctx.box();
  const theme::ResolvedStyle& rs = ctx.resolve("picker.panel", 0);
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(rs.radius));
  if (const auto layers = ctx.ui().services().tokens().shadow("xl")) {
    for (auto it = layers->rbegin(); it != layers->rend(); ++it) {
      render::ShadowSpec spec;
      spec.offsetX = ctx.px(it->offsetX);
      spec.offsetY = ctx.px(it->offsetY);
      spec.blur = ctx.px(it->blur);
      spec.spread = ctx.px(it->spread);
      spec.color = ctx.color(it->color);
      ctx.painter().shadow(box, radii, spec);
    }
  }
  ctx.fillBox(rs, box);
}

}  // namespace r1ui::widgets
