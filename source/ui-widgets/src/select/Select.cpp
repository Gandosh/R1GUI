// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Select.h.
// Invariants: open_ is true exactly while an overlay of this select exists (the overlay's onClosed
//   callback is the only place that resets it, found again by widget id so a destroyed select is never
//   touched); selected_ always indexes an Item entry of the current model; the change callback runs after
//   the popup is closed and the widget is re-checked for liveness afterwards.
// Callers: UiContext (events, paint), SelectList (choose, applyFilter), tests.
#include "r1ui/widgets/select/Select.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/select/SelectList.h"
#include "r1ui/widgets/textinput/FieldChrome.h"

namespace r1ui::widgets {

namespace {

using core::events::Button;
using core::events::FocusReason;
using core::events::Key;
namespace Mod = core::events::Mod;

constexpr double kChevron = 12.0;
constexpr double kChevronRight = 7.0;  // border 1 + padding 6
constexpr double kChevronGap = 4.0;
constexpr double kPopupGap = 2.0;      // measured: the list starts 2 px below the trigger

bool isTypeKey(Key k, uint8_t mods) {
  if ((mods & Mod::kCtrl) != 0 && (mods & Mod::kAlt) == 0) return false;
  const auto v = static_cast<uint16_t>(k);
  return (v >= 'A' && v <= 'Z') || (v >= '0' && v <= '9');
}

}  // namespace

Select::Select() = default;

std::span<const theme::StyleRuleEntry> Select::styleRows() { return fieldStyleRows(); }

void Select::onAttached() {
  setFocusable(true);
  const theme::ResolvedStyle& rs = ui().services().resolve("input.panel", 0);
  style().height = core::layout::Length::px(rs.height);
  style().minHeight = core::layout::Length::px(rs.height);
}

void Select::onDetached() { close(); }

// ---- entries and selection -------------------------------------------------------------------------

bool Select::setEntries(std::vector<SelectEntry> entries) {
  const bool hadSelection = selected_.has_value();
  const std::string previous(selectedValue());
  if (entries.size() > SelectModel::kMaxEntries) return false;
  close();
  if (!model_.setEntries(std::move(entries))) return false;
  selected_.reset();
  if (hadSelection) {
    const std::optional<size_t> again = model_.indexOfValue(previous);
    if (again) selected_ = again;
  }
  requestPaint();
  return true;
}

bool Select::addItem(std::string label, std::string value, bool disabled) {
  std::vector<SelectEntry> next = model_.entries();
  next.push_back({SelectEntryKind::Item, std::move(label), std::move(value), disabled});
  return setEntries(std::move(next));
}

bool Select::addGroup(std::string label) {
  std::vector<SelectEntry> next = model_.entries();
  next.push_back({SelectEntryKind::Group, std::move(label), {}, false});
  return setEntries(std::move(next));
}

bool Select::addSeparator() {
  std::vector<SelectEntry> next = model_.entries();
  next.push_back({SelectEntryKind::Separator, {}, {}, false});
  return setEntries(std::move(next));
}

void Select::clearEntries() { setEntries({}); }

bool Select::setSelectedIndex(std::optional<size_t> index) {
  if (index) {
    const std::vector<SelectEntry>& e = model_.entries();
    if (*index >= e.size() || e[*index].kind != SelectEntryKind::Item) return false;
  }
  if (index == selected_) return true;
  selected_ = index;
  requestPaint();
  return true;
}

bool Select::setSelectedValue(std::string_view value) {
  const std::optional<size_t> index = model_.indexOfValue(value);
  return index ? setSelectedIndex(index) : false;
}

std::string_view Select::selectedValue() const { return selected_ ? std::string_view(model_.entries()[*selected_].value) : std::string_view(); }

std::string_view Select::selectedLabel() const { return selected_ ? std::string_view(model_.entries()[*selected_].label) : std::string_view(); }

void Select::setPlaceholder(std::string placeholder) {
  if (placeholder == placeholder_) return;
  placeholder_ = std::move(placeholder);
  requestPaint();
}

void Select::setCompact(bool compact) { compact_ = compact; }

void Select::setSearchable(bool searchable) { searchable_ = searchable; }

// ---- popup ----------------------------------------------------------------------------------------------

bool Select::open() {
  if (open_) return true;
  if (!enabled() || !ui().alive(ui().overlays().layer())) return false;
  OverlayOptions options;
  options.anchor = ui().absRect(id());
  options.placement = Placement::BelowStart;
  options.gap = kPopupGap;
  options.matchAnchorWidth = true;
  options.anchorWidget = id();
  options.surface = OverlaySurface::Menu;
  options.focusOnOpen = true;
  options.restoreFocus = true;
  options.dismissOnOutsidePress = true;
  options.outsidePressPassesThrough = true;
  options.dismissOnEscape = true;
  options.dismissOnWindowDeactivate = true;
  UiContext* const context = &ui();
  const core::tree::WidgetId self = id();
  options.onClosed = [context, self](DismissReason) {
    if (Select* select = context->objectAs<Select>(self)) select->onPopupClosed();
  };
  const OverlayHandle handle = ui().overlays().open(options);
  if (!handle.valid()) return false;
  try {
    SelectList& list = ui().create<SelectList>(handle.host, id());
    list_ = list.id();
  } catch (const std::length_error&) {
    ui().overlays().close(handle.id);
    return false;
  }
  overlay_ = handle.id;
  open_ = true;
  requestPaint();
  return true;
}

void Select::close() {
  if (!open_) return;
  const OverlayId overlay = overlay_;
  if (!ui().overlays().close(overlay)) onPopupClosed();
}

void Select::onPopupClosed() {
  open_ = false;
  overlay_ = {};
  list_ = {};
  model_.setFilter({});
  requestPaint();
}

void Select::applyFilter(std::string_view text) { model_.setFilter(text); }

void Select::choose(size_t entryIndex) {
  const std::vector<SelectEntry>& entries = model_.entries();
  if (entryIndex >= entries.size() || !SelectModel::selectable(entries[entryIndex])) return;
  const bool changed = selected_ != entryIndex;
  selected_ = entryIndex;
  const std::string value = entries[entryIndex].value;
  close();
  if (!ui().alive(id())) return;
  requestPaint();
  if (changed && onChanged_) {
    const ChangeCallback callback = onChanged_;
    callback(entryIndex, value);
  }
}

// ---- WidgetObject -----------------------------------------------------------------------------------------

float Select::paintOpacity() const { return static_cast<float>(ui().services().resolve("input.panel", styleState()).opacity); }

std::string_view Select::accessibleName() const {
  if (!WidgetObject::accessibleName().empty()) return WidgetObject::accessibleName();
  return selected_ ? selectedLabel() : std::string_view(placeholder_);
}

void Select::onStateChanged(uint16_t previous) {
  if (hasState(StateFlag::kDisabled) && (previous & StateFlag::kDisabled) == 0) close();
}

void Select::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style("input.panel");
  const render::Rect box = ctx.box();
  paintFieldBox(ctx, rs, animatedFieldColors(ctx, rs, 0), box);
  const core::layout::Rect r = ctx.rect();
  const float scale = ctx.scale();
  const double left = r.x + rs.border.width + rs.paddingX;
  const double chevronLeft = static_cast<double>(r.right()) - kChevronRight - kChevron;
  const double lineTop = r.y + rs.border.width + rs.paddingY;
  const render::Rect textBox{static_cast<float>(left) * scale, static_cast<float>(lineTop) * scale, static_cast<float>(chevronLeft - kChevronGap - left) * scale,
                             ctx.px(rs.text.lineHeight)};
  const bool placeholder = !selected_;
  const std::string_view label = placeholder ? std::string_view(placeholder_) : selectedLabel();
  if (!label.empty() && textBox.w > 0.0f) {
    TextOptions options;
    if (placeholder) options.color = ctx.color("surface", 0.5);
    ctx.drawText(label, rs.text, textBox, options);
  }
  ctx.drawIcon("chevron-down", kChevron, ctx.toPhysical(chevronLeft, r.y + (r.h - kChevron) / 2.0, kChevron, kChevron), ctx.color("muted"));
}

void Select::onPointerDown(Event& e) {
  if (e.button != Button::Left || !enabled()) return;
  e.markHandled();
  ui().router().focus(id(), FocusReason::Pointer);
  if (!ui().alive(id())) return;
  open();  // pressing the trigger of an open list is consumed by the overlay layer, which closes it
}

void Select::onKeyDown(Event& e) {
  if (!enabled()) return;
  if (open_) {
    // Before the list has taken focus (the first frame after opening) the trigger relays the keys.
    SelectList* list = ui().objectAs<SelectList>(list_);
    if (e.key == Key::Escape) {
      e.markHandled();
      close();
    } else if (list != nullptr && !list->focused()) {
      list->onKeyDown(e);
    }
    return;
  }
  switch (e.key) {
    case Key::Enter:
    case Key::Space:
    case Key::Down:
    case Key::Up:
      e.markHandled();
      open();
      return;
    default: break;
  }
  if (isTypeKey(e.key, e.modifiers)) e.markHandled();  // typing opens the list; shortcuts must not see the letter
}

void Select::onTextInput(Event& e) {
  if (!enabled()) return;
  if ((e.modifiers & (Mod::kCtrl | Mod::kMeta)) != 0 && (e.modifiers & Mod::kAlt) == 0) return;
  e.markHandled();
  if (!open_ && !open()) return;
  if (SelectList* list = ui().objectAs<SelectList>(list_)) {
    if (!list->focused()) list->typeAhead(e.codePoint);
  }
}

}  // namespace r1ui::widgets
