// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PickerDropdown.h and its private list item widget.
// Invariants: the list items refer to the trigger by WidgetId and re-check that it is alive; the
//   overlay is closed when the trigger is destroyed or detached; indices are validated against the
//   item vector at every use.
// Callers: the colour picker and the gradient editor.
#include "r1ui/widgets/colorpicker/PickerDropdown.h"

#include <algorithm>

#include "r1ui/widgets/popover/OverlayWatch.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using theme::State::kDisabled;
using theme::State::kFocus;
using theme::State::kHover;
using theme::State::kNone;
using theme::StyleProperty;
using theme::StyleRuleEntry;

constexpr double kItemHeight = 28.0;
constexpr double kItemTextLeft = 24.0;

const StyleRuleEntry kRows[] = {
    {"picker.dropdown", kNone, StyleProperty::Background, "color:panel-field"},
    {"picker.dropdown", kHover, StyleProperty::Background, "color:panel-field-hover"},
    {"picker.dropdown", kNone, StyleProperty::Foreground, "color:surface"},
    {"picker.dropdown", kNone, StyleProperty::BorderColor, "transparent"},
    {"picker.dropdown", kFocus, StyleProperty::BorderColor, "color:panel-focus"},
    {"picker.dropdown", kNone, StyleProperty::BorderWidth, "number:1"},
    {"picker.dropdown", kNone, StyleProperty::Radius, "radius:panel"},
    {"picker.dropdown", kNone, StyleProperty::PaddingX, "number:6"},
    {"picker.dropdown", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"picker.dropdown", kNone, StyleProperty::LineHeight, "lineHeight:xs"},
    {"picker.dropdown", kNone, StyleProperty::FontWeight, "weight:medium"},
    {"picker.dropdown", kDisabled, StyleProperty::Opacity, "number:0.6"},

    {"picker.dropdown.item", kNone, StyleProperty::Background, "transparent"},
    {"picker.dropdown.item", kHover, StyleProperty::Background, "color:hover"},
    {"picker.dropdown.item", kNone, StyleProperty::Foreground, "color:surface"},
    {"picker.dropdown.item", kNone, StyleProperty::Radius, "radius:panel"},
    {"picker.dropdown.item", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"picker.dropdown.item", kNone, StyleProperty::LineHeight, "lineHeight:xs"},
    {"picker.dropdown.item", kNone, StyleProperty::FontWeight, "weight:normal"},
};

// One row of the open list. Hover highlights, a click chooses.
class PickerMenuItem final : public WidgetObject {
 public:
  PickerMenuItem(core::tree::WidgetId owner, int index, std::string text) : owner_(owner), index_(index), text_(std::move(text)) {}
  const char* typeName() const override { return "PickerMenuItem"; }
  void onAttached() override {
    style().height = core::layout::Length::px(kItemHeight);
    style().flexShrink = 0.0;
  }
  Cursor cursor() const override { return Cursor::Pointer; }
  std::string_view accessibleName() const override { return text_; }
  void paint(PaintContext& ctx) override {
    PickerDropdown* owner = ui().objectAs<PickerDropdown>(owner_);
    const bool highlighted = owner != nullptr && owner->highlighted() == index_;
    const theme::ResolvedStyle& rs = ctx.resolve("picker.dropdown.item", highlighted ? kHover : kNone);
    ctx.fillBox(rs);
    const render::Rect box = ctx.box();
    TextOptions options;
    options.padLeft = kItemTextLeft;
    options.padRight = 6;
    ctx.drawText(text_, rs.text, box, options);
    if (owner != nullptr && owner->selected() == index_) {
      ctx.drawIcon("check", 12, {box.x + ctx.px(6), box.y, ctx.px(12), box.h}, ctx.color(rs.text.color));
    }
  }
  void onPointerEnter(Event&) override {
    if (PickerDropdown* owner = ui().objectAs<PickerDropdown>(owner_)) owner->highlight(index_);
  }
  void onClick(Event& e) override {
    if (e.button != core::events::Button::Left) return;
    e.markHandled();
    if (PickerDropdown* owner = ui().objectAs<PickerDropdown>(owner_)) owner->choose(index_);
  }

 private:
  core::tree::WidgetId owner_;
  int index_;
  std::string text_;
};

}  // namespace

std::span<const StyleRuleEntry> PickerDropdown::styleRows() { return kRows; }

void PickerDropdown::onAttached() {
  style().height = core::layout::Length::px(ui().services().tokens().space("control").value_or(26.0));
  setFocusable(true);
}

void PickerDropdown::onDetached() { close(); }

float PickerDropdown::paintOpacity() const { return static_cast<float>(ui().services().resolve("picker.dropdown", styleState()).opacity); }

std::string_view PickerDropdown::accessibleName() const {
  if (!WidgetObject::accessibleName().empty()) return WidgetObject::accessibleName();
  return selected_ >= 0 && static_cast<size_t>(selected_) < items_.size() ? std::string_view(items_[static_cast<size_t>(selected_)]) : std::string_view();
}

void PickerDropdown::setItems(std::vector<std::string> items) {
  close();
  items_ = std::move(items);
  if (selected_ >= static_cast<int>(items_.size())) selected_ = -1;
  requestPaint();
}

void PickerDropdown::setSelected(int index) {
  const int next = index >= 0 && static_cast<size_t>(index) < items_.size() ? index : -1;
  if (next == selected_) return;
  selected_ = next;
  requestPaint();
}

bool PickerDropdown::isOpen() const { return overlay_.valid() && ui().overlays().isOpen(overlay_); }

void PickerDropdown::open() {
  if (isOpen() || items_.empty() || !enabled()) return;
  OverlayOptions options;
  options.anchor = ui().absRect(id());
  options.placement = Placement::BelowStart;
  options.gap = 4.0;
  options.matchAnchorWidth = true;
  options.anchorWidget = id();
  options.surface = OverlaySurface::Menu;
  options.dismissOnOutsidePress = true;
  options.outsidePressPassesThrough = false;
  options.restoreFocus = false;
  const core::tree::WidgetId self = id();
  options.onClosed = [this, self](DismissReason) {
    if (ui().alive(self)) {
      overlay_ = {};
      highlighted_ = -1;
      requestPaint();
    }
  };
  const OverlayHandle handle = ui().overlays().open(options);
  if (!handle.valid()) return;
  try {
    for (size_t i = 0; i < items_.size(); ++i) ui().create<PickerMenuItem>(handle.host, self, static_cast<int>(i), items_[i]);
    // The list follows its field when the field moves or is resized, and closes with it.
    ui().create<OverlayWatch>(handle.host, handle.id, self, self);
  } catch (...) {
    ui().overlays().close(handle.id, DismissReason::Programmatic);
    throw;
  }
  overlay_ = handle.id;
  highlighted_ = selected_ >= 0 ? selected_ : 0;
  requestPaint();
}

void PickerDropdown::close() {
  if (!overlay_.valid()) return;
  const OverlayId overlay = overlay_;
  overlay_ = {};
  highlighted_ = -1;
  ui().overlays().close(overlay);
}

void PickerDropdown::highlight(int index) {
  if (index < 0 || static_cast<size_t>(index) >= items_.size() || index == highlighted_) return;
  highlighted_ = index;
  ui().invalidator().requestPaint(ui().overlays().hostOf(overlay_));
}

void PickerDropdown::moveHighlight(int delta) {
  if (items_.empty()) return;
  const int count = static_cast<int>(items_.size());
  const int from = highlighted_ < 0 ? (delta > 0 ? -1 : count) : highlighted_;
  highlight(std::clamp(from + delta, 0, count - 1));
}

void PickerDropdown::choose(int index) {
  if (index < 0 || static_cast<size_t>(index) >= items_.size()) return;
  const core::tree::WidgetId self = id();
  selected_ = index;
  close();
  if (!ui().alive(self)) return;
  requestPaint();
  if (onSelect) onSelect(index);
}

void PickerDropdown::onClick(Event& e) {
  if (e.button != core::events::Button::Left) return;
  e.markHandled();
  if (isOpen()) close();
  else open();
}

void PickerDropdown::onKeyDown(Event& e) {
  using core::events::Key;
  if (isOpen()) {
    switch (e.key) {
      case Key::Down: moveHighlight(1); break;
      case Key::Up: moveHighlight(-1); break;
      case Key::Home: highlight(0); break;
      case Key::End: highlight(static_cast<int>(items_.size()) - 1); break;
      case Key::Enter:
      case Key::Space: choose(highlighted_); break;
      case Key::Escape: close(); break;
      case Key::Tab: close(); return;
      default: return;
    }
    e.markHandled();
    return;
  }
  if (e.key == Key::Enter || e.key == Key::Space || e.key == Key::Down || e.key == Key::Up) {
    open();
    e.markHandled();
  }
}

void PickerDropdown::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style("picker.dropdown");
  ctx.fillBox(rs);
  const render::Rect box = ctx.box();
  if (selected_ >= 0 && static_cast<size_t>(selected_) < items_.size()) {
    TextOptions options;
    options.padLeft = rs.paddingX;
    options.padRight = 24;
    ctx.drawText(items_[static_cast<size_t>(selected_)], rs.text, box, options);
  }
  ctx.drawIcon("chevron-down", 12, {box.x + box.w - ctx.px(6 + 12), box.y, ctx.px(12), box.h}, ctx.color("muted"));
}

void PickerDropdown::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.box(), ctx.px(ctx.style("picker.dropdown").radius));
}

}  // namespace r1ui::widgets
