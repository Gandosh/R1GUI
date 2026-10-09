// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Swatches.h and the private swatch widget.
// Invariants: swatch widgets are rebuilt from swatches_ (the single source of truth) whenever the
//   list changes; a swatch refers to its row by WidgetId and index and re-validates both before
//   acting, so a callback that rebuilds the list cannot leave a stale swatch acting on a new index.
// Callers: the colour picker; tests.
#include "r1ui/widgets/colorpicker/Swatches.h"

#include <algorithm>

#include "r1ui/widgets/colorpicker/PickerDraw.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

namespace events = core::events;
using theme::State::kNone;
using theme::StyleProperty;
using theme::StyleRuleEntry;

constexpr double kSwatchSize = 20.0;
constexpr double kHeaderHeight = 24.0;

const StyleRuleEntry kRows[] = {
    {"picker.swatch", kNone, StyleProperty::BorderColor, "color:border"},
    {"picker.swatch", theme::State::kSelected, StyleProperty::BorderColor, "color:accent"},
    {"picker.swatch", kNone, StyleProperty::BorderWidth, "number:1"},
    {"picker.swatch", kNone, StyleProperty::Radius, "radius:panel"},
    {"picker.swatches.title", kNone, StyleProperty::Foreground, "color:muted"},
    {"picker.swatches.title", kNone, StyleProperty::FontSize, "fontSize:11"},
    {"picker.swatches.title", kNone, StyleProperty::LineHeight, "number:16"},
    {"picker.swatches.title", kNone, StyleProperty::FontWeight, "weight:normal"},
};

bool sameColour(const color::Rgba& a, const color::Rgba& b) {
  return color::to8(a.rgb.r) == color::to8(b.rgb.r) && color::to8(a.rgb.g) == color::to8(b.rgb.g) && color::to8(a.rgb.b) == color::to8(b.rgb.b) &&
         color::to8(a.a) == color::to8(b.a);
}

class PickerSwatch final : public WidgetObject {
 public:
  PickerSwatch(core::tree::WidgetId row, size_t index, color::Rgba colour) : row_(row), index_(index), colour_(colour) {}
  const char* typeName() const override { return "PickerSwatch"; }
  void onAttached() override {
    style().width = core::layout::Length::px(kSwatchSize);
    style().height = core::layout::Length::px(kSwatchSize);
    style().flexShrink = 0.0;
    setFocusable(true);
  }
  Cursor cursor() const override { return Cursor::Pointer; }
  std::string_view accessibleName() const override { return name_; }
  void setName(std::string n) { name_ = std::move(n); }

  void paint(PaintContext& ctx) override {
    const theme::ResolvedStyle& rs = ctx.style("picker.swatch");
    const render::Rect box = ctx.box();
    const float radius = ctx.px(rs.radius);
    pickerdraw::drawCheckerboard(ctx, box, radius, 4.0, ctx.color("checkerboard"), ctx.color("checkerboard-muted"));
    ctx.painter().fillRoundedRect(box, render::CornerRadii::uniform(radius),
                                  {static_cast<float>(colour_.rgb.r), static_cast<float>(colour_.rgb.g), static_cast<float>(colour_.rgb.b),
                                   static_cast<float>(colour_.a)});
    ctx.painter().border(box, render::CornerRadii::uniform(radius), ctx.px(rs.border.width), ctx.color(rs.border.color));
  }
  void paintOver(PaintContext& ctx) override {
    if (focusVisible()) ctx.focusRing(ctx.box(), ctx.px(ctx.style("picker.swatch").radius));
  }

  void onClick(Event& e) override {
    if (e.button != events::Button::Left) return;
    e.markHandled();
    SwatchRow* row = ui().objectAs<SwatchRow>(row_);
    if (row == nullptr) return;
    if ((e.modifiers & events::Mod::kAlt) != 0) row->removeAt(index_);
    else if (row->onApply) row->onApply(colour_);
  }
  void onKeyDown(Event& e) override {
    SwatchRow* row = ui().objectAs<SwatchRow>(row_);
    if (row == nullptr) return;
    if (e.key == events::Key::Enter || e.key == events::Key::Space) {
      e.markHandled();
      if (row->onApply) row->onApply(colour_);
    } else if (e.key == events::Key::Delete || e.key == events::Key::Backspace) {
      e.markHandled();
      row->removeAt(index_);
    }
  }

 private:
  core::tree::WidgetId row_;
  size_t index_;
  color::Rgba colour_;
  std::string name_;
};

}  // namespace

std::span<const StyleRuleEntry> SwatchRow::styleRows() { return kRows; }

void SwatchRow::onAttached() {
  core::layout::Style& s = style();
  s.direction = core::layout::FlexDirection::Column;
  s.flexShrink = 0.0;
  s.padding[core::layout::kTop] = 5.0;  // 1 px divider + 4 px
  s.gapRow = 4.0;

  PickerBox& header = ui().create<PickerBox>(id(), "SwatchHeader");
  header_ = header.id();
  header.style().direction = core::layout::FlexDirection::Row;
  header.style().alignItems = core::layout::Align::Center;
  header.style().justifyContent = core::layout::Justify::End;
  header.style().height = core::layout::Length::px(kHeaderHeight);
  header.style().gapColumn = 0.0;

  PickerButton& add = ui().create<PickerButton>(header.id(), "plus", PickerButton::Look::Plain, 20.0, 14.0);
  add_ = add.id();
  add.setTooltipAndName("Add swatch");
  add.onActivate = [this] { addCurrent(); };
  PickerButton& save = ui().create<PickerButton>(header.id(), "save", PickerButton::Look::Plain, 20.0, 14.0);
  save_ = save.id();
  save.setTooltipAndName("Save swatches");
  save.onActivate = [this] {
    if (onSave) onSave();
  };

  PickerBox& flow = ui().create<PickerBox>(id(), "SwatchFlow");
  flow_ = flow.id();
  flow.style().direction = core::layout::FlexDirection::Row;
  flow.style().wrap = core::layout::FlexWrap::Wrap;
  flow.style().gapRow = 4.0;
  flow.style().gapColumn = 4.0;
  flow.style().display = core::layout::Display::None;  // shown once there is a swatch
}

PickerButton& SwatchRow::addButton() const { return *ui().objectAs<PickerButton>(add_); }
PickerButton& SwatchRow::saveButton() const { return *ui().objectAs<PickerButton>(save_); }

void SwatchRow::paint(PaintContext& ctx) {
  const render::Rect box = ctx.box();
  ctx.painter().fillRect({box.x, box.y, box.w, ctx.hairline()}, ctx.color("border"));
  const core::layout::Rect h = ui().absRect(header_);
  const theme::ResolvedStyle& rs = ctx.resolve("picker.swatches.title", 0);
  TextOptions options;
  ctx.drawText("Color swatches", rs.text, ctx.toPhysical(h.x, h.y, std::max(0, h.w - 48), h.h), options);
}

void SwatchRow::setSwatches(std::vector<color::Rgba> swatches) {
  if (swatches.size() > kMaxSwatches) swatches.resize(kMaxSwatches);
  for (color::Rgba& c : swatches) c = color::sanitized(c);
  swatches_ = std::move(swatches);
  rebuild();
}

void SwatchRow::setCurrent(const color::Rgba& current) {
  current_ = color::sanitized(current);
  size_t i = 0;
  ui().tree().forEachChild(flow_, [&](core::tree::WidgetId c) {
    if (WidgetObject* o = ui().object(c)) o->setSelected(i < swatches_.size() && sameColour(swatches_[i], current_));
    ++i;
  });
}

void SwatchRow::rebuild() {
  std::vector<core::tree::WidgetId> old;
  ui().tree().forEachChild(flow_, [&](core::tree::WidgetId c) { old.push_back(c); });
  for (const core::tree::WidgetId c : old) ui().destroy(c);
  for (size_t i = 0; i < swatches_.size(); ++i) {
    PickerSwatch& s = ui().create<PickerSwatch>(flow_, id(), i, swatches_[i]);
    s.setName("Swatch " + color::formatHex(swatches_[i]));
    s.setTooltip("#" + color::formatHex(swatches_[i]));
  }
  if (WidgetObject* flow = ui().object(flow_)) {
    flow->style().display = swatches_.empty() ? core::layout::Display::None : core::layout::Display::Flex;
    flow->requestLayout();
  }
  setCurrent(current_);
  requestLayout();
  requestPaint();
}

bool SwatchRow::addCurrent() {
  if (swatches_.size() >= kMaxSwatches) return false;
  for (const color::Rgba& s : swatches_) {
    if (sameColour(s, current_)) return false;
  }
  swatches_.push_back(current_);
  rebuild();
  changed();
  return true;
}

bool SwatchRow::removeAt(size_t index) {
  if (index >= swatches_.size()) return false;
  const core::tree::WidgetId self = id();
  swatches_.erase(swatches_.begin() + static_cast<std::ptrdiff_t>(index));
  rebuild();
  if (ui().alive(self)) changed();
  return true;
}

void SwatchRow::changed() {
  if (onSwatchesChanged) onSwatchesChanged(swatches_);
}

}  // namespace r1ui::widgets
