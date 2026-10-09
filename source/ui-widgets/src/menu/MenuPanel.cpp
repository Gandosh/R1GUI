// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of MenuPanel.h: row construction, highlight, keyboard, type-ahead, scrolling.
// Invariants: highlighted_ is -1 or the index of a selectable row; scroll_ stays within
//   [0, maxScroll()]; widgets_ has one entry per item (an invalid id for a hidden heading); no
//   listener call happens after onDetached.
// Callers: MenuController (creation and listener), MenuItemWidget (reports), the Router (events).
#include "r1ui/widgets/menu/MenuPanel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace events = core::events;
using core::tree::WidgetId;

namespace {

constexpr double kWheelStep = 48.0;      // logical px per wheel notch
constexpr uint64_t kPrefixResetMs = 1000;
constexpr double kScrollBarWidth = 6.0;
constexpr double kScrollBarInset = 1.0;
constexpr double kMinThumb = 20.0;
constexpr double kBorderAndPadding = 10.0;  // host border 1 + padding 4, each side, around the panel

bool selectableKind(MenuItemKind kind) { return kind != MenuItemKind::Separator && kind != MenuItemKind::Heading; }

void appendUtf8(std::string& out, char32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

char foldAscii(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool startsWithFolded(const std::string& text, const std::string& prefix) {
  if (prefix.size() > text.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (foldAscii(text[i]) != foldAscii(prefix[i])) return false;
  }
  return true;
}

// Cleans one spec at the boundary: texts are sanitised, nested submenus are cleaned recursively up
// to the depth limit (deeper children are dropped).
void sanitizeSpec(MenuItemSpec& spec, int depth) {
  spec.id = sanitizeMenuText(spec.id);
  spec.label = sanitizeMenuText(spec.label);
  spec.description = sanitizeMenuText(spec.description);
  spec.icon = sanitizeMenuText(spec.icon);
  spec.shortcut = sanitizeMenuText(spec.shortcut);
  spec.tooltip = sanitizeMenuText(spec.tooltip);
  if (spec.children.size() > kMaxMenuItems) spec.children.resize(kMaxMenuItems);
  if (depth >= kMaxMenuDepth) spec.children.clear();
  for (MenuItemSpec& child : spec.children) sanitizeSpec(child, depth + 1);
}

}  // namespace

// ---- MenuList -----------------------------------------------------------------------------------

void MenuList::onAttached() {
  core::layout::Style& s = style();
  s.direction = core::layout::FlexDirection::Column;
  s.alignItems = core::layout::Align::Stretch;
  s.flexShrink = 0.0;
  node().flags.hitTestTransparent = true;
}

// ---- MenuPanel ----------------------------------------------------------------------------------

MenuPanel::MenuPanel(std::vector<MenuItemSpec> items, double minWidth, const MenuLook& look, std::shared_ptr<MenuPanelListener> listener)
    : items_(std::move(items)), minWidth_(std::isfinite(minWidth) ? std::max(0.0, minWidth) : 0.0), look_(look), listener_(std::move(listener)) {
  if (items_.size() > kMaxMenuItems) {
    items_.resize(kMaxMenuItems);
    truncated_ = true;
  }
  for (MenuItemSpec& spec : items_) sanitizeSpec(spec, 1);
  // A heading is shown only when at least one entry follows it before the next heading or separator.
  shown_.assign(items_.size(), true);
  for (size_t i = 0; i < items_.size(); ++i) {
    if (items_[i].kind != MenuItemKind::Heading) continue;
    bool entry = false;
    for (size_t j = i + 1; j < items_.size() && items_[j].kind != MenuItemKind::Separator && items_[j].kind != MenuItemKind::Heading; ++j) entry = true;
    shown_[i] = entry;
  }
}

void MenuPanel::onAttached() {
  core::layout::Style& s = style();
  s.direction = core::layout::FlexDirection::Column;
  s.alignItems = core::layout::Align::Stretch;
  s.overflow = core::layout::Overflow::Hidden;
  s.flexShrink = 1.0;
  s.minHeight = core::layout::Length::px(0);
  if (minWidth_ > kBorderAndPadding) s.minWidth = core::layout::Length::px(minWidth_ - kBorderAndPadding);
  setFocusable(true);

  list_ = ui().create<MenuList>(id()).id();
  widgets_.assign(items_.size(), WidgetId{});
  try {
    for (size_t i = 0; i < items_.size(); ++i) {
      if (!shown_[i]) continue;
      const MenuItemSpec& spec = items_[i];
      switch (spec.kind) {
        case MenuItemKind::Separator: widgets_[i] = ui().create<MenuSeparatorWidget>(list_).id(); break;
        case MenuItemKind::Heading: widgets_[i] = ui().create<MenuHeadingWidget>(list_, spec.label).id(); break;
        default: widgets_[i] = ui().create<MenuItemWidget>(list_, spec, id(), static_cast<int>(i), look_).id(); break;
      }
    }
  } catch (const std::length_error&) {
    truncated_ = true;  // the tree is full: the rows that fit stay, the rest is not selectable
    for (size_t i = 0; i < items_.size(); ++i) {
      if (!widgets_[i].valid()) shown_[i] = false;
    }
  }
}

WidgetId MenuPanel::itemWidget(int index) const {
  if (index < 0 || static_cast<size_t>(index) >= widgets_.size()) return {};
  return widgets_[static_cast<size_t>(index)];
}

bool MenuPanel::selectable(int index) const {
  if (index < 0 || static_cast<size_t>(index) >= items_.size()) return false;
  const size_t i = static_cast<size_t>(index);
  return shown_[i] && widgets_[i].valid() && selectableKind(items_[i].kind) && items_[i].enabled;
}

// ---- highlight ----------------------------------------------------------------------------------

bool MenuPanel::setHighlight(int index, bool scrollIntoView) {
  if (index != -1 && !selectable(index)) return false;
  if (index == highlighted_) {
    if (scrollIntoView && index >= 0) ensureVisible(index);
    return true;
  }
  if (MenuItemWidget* old = ui().objectAs<MenuItemWidget>(itemWidget(highlighted_))) old->setHighlighted(false);
  highlighted_ = index;
  if (MenuItemWidget* now = ui().objectAs<MenuItemWidget>(itemWidget(index))) now->setHighlighted(true);
  if (scrollIntoView && index >= 0) ensureVisible(index);
  return true;
}

int MenuPanel::nextSelectable(int from, int delta) const {
  for (int i = from + delta; i >= 0 && i < itemCount(); i += delta) {
    if (selectable(i)) return i;
  }
  return -1;
}

bool MenuPanel::moveHighlight(int delta) {
  if (delta == 0) return false;
  const int start = highlighted_ >= 0 ? highlighted_ : (delta > 0 ? -1 : itemCount());
  const int next = nextSelectable(start, delta > 0 ? 1 : -1);
  return next >= 0 && setHighlight(next);
}

bool MenuPanel::highlightFirst() {
  const int first = nextSelectable(-1, 1);
  return first >= 0 && setHighlight(first);
}

bool MenuPanel::highlightLast() {
  const int last = nextSelectable(itemCount(), -1);
  return last >= 0 && setHighlight(last);
}

// ---- reports from the rows ----------------------------------------------------------------------

void MenuPanel::itemPointerMoved(int index) {
  if (pointerItem_ == index) return;  // the pointer moved inside the same row
  pointerItem_ = index;
  if (!selectable(index)) return;
  setHighlight(index, false);
  if (listener_ != nullptr) listener_->panelItemEntered(*this, index);
}

void MenuPanel::itemPointerLeft(int index) {
  if (pointerItem_ == index) pointerItem_ = -1;
  if (listener_ != nullptr) listener_->panelItemLeft(*this, index);
  if (highlighted_ == index && (!listener_ || !listener_->panelSubmenuOpenFor(*this, index))) setHighlight(-1, false);
}

void MenuPanel::itemActivated(int index, bool fromKeyboard) {
  if (!selectable(index) || listener_ == nullptr) return;
  listener_->panelItemActivated(*this, index, fromKeyboard);
}

MenuItemSpec MenuPanel::applyActivationState(int index) {
  if (index < 0 || static_cast<size_t>(index) >= items_.size()) return {};
  MenuItemSpec& spec = items_[static_cast<size_t>(index)];
  const auto sync = [this](int i) {
    if (MenuItemWidget* w = ui().objectAs<MenuItemWidget>(itemWidget(i))) w->setChecked(items_[static_cast<size_t>(i)].checked);
  };
  if (spec.kind == MenuItemKind::Check) {
    spec.checked = !spec.checked;
    sync(index);
  } else if (spec.kind == MenuItemKind::Radio) {
    // The radio group is the run of contiguous radio rows around the activated one.
    int first = index;
    int last = index;
    while (first > 0 && items_[static_cast<size_t>(first - 1)].kind == MenuItemKind::Radio) --first;
    while (last + 1 < itemCount() && items_[static_cast<size_t>(last + 1)].kind == MenuItemKind::Radio) ++last;
    for (int i = first; i <= last; ++i) {
      items_[static_cast<size_t>(i)].checked = i == index;
      sync(i);
    }
  }
  return spec;
}

bool MenuPanel::refreshItem(int index, const MenuItemSpec& live) {
  if (index < 0 || static_cast<size_t>(index) >= items_.size()) return false;
  const size_t i = static_cast<size_t>(index);
  MenuItemSpec& spec = items_[i];
  if (!shown_[i] || !widgets_[i].valid() || !selectableKind(spec.kind)) return false;
  spec.label = sanitizeMenuText(live.label);
  spec.shortcut = sanitizeMenuText(live.shortcut);
  spec.tooltip = sanitizeMenuText(live.tooltip);
  spec.enabled = live.enabled;
  spec.checked = live.checked;
  if (MenuItemWidget* w = ui().objectAs<MenuItemWidget>(widgets_[i])) w->refresh(spec);
  if (!spec.enabled && highlighted_ == index) setHighlight(-1, false);
  return true;
}

// ---- scrolling ----------------------------------------------------------------------------------

double MenuPanel::maxScroll() const {
  const core::layout::Rect panel = ui().absRect(id());
  const core::layout::Rect list = ui().absRect(list_);
  return std::max(0.0, static_cast<double>(list.h) - static_cast<double>(panel.h));
}

void MenuPanel::scrollTo(double offset) {
  const double clamped = std::clamp(std::isfinite(offset) ? offset : 0.0, 0.0, maxScroll());
  if (clamped == scroll_) return;
  scroll_ = clamped;
  if (WidgetObject* list = ui().object(list_)) {
    list->style().margin[core::layout::kTop] = core::layout::Length::px(-scroll_);
    list->requestLayout();
  }
  requestPaint();
}

void MenuPanel::ensureVisible(int index) {
  const core::layout::Rect panel = ui().absRect(id());
  const core::layout::Rect list = ui().absRect(list_);
  const core::layout::Rect row = ui().absRect(itemWidget(index));
  if (panel.empty() || row.empty() || list.empty()) return;
  // Work in list coordinates: the row's offset inside the list does not depend on the scroll offset
  // (the rectangles may predate a scroll change that has not been laid out yet).
  const double top = static_cast<double>(row.y - list.y);
  const double bottom = top + static_cast<double>(row.h);
  if (top < scroll_) scrollTo(top);
  else if (bottom > scroll_ + static_cast<double>(panel.h)) scrollTo(bottom - static_cast<double>(panel.h));
}

void MenuPanel::onPointerWheel(Event& e) {
  if (maxScroll() <= 0.0) return;
  scrollTo(scroll_ - e.wheelY * kWheelStep);
  e.markHandled();
}

void MenuPanel::paintOver(PaintContext& ctx) {
  const double range = maxScroll();
  if (range <= 0.0) return;
  const core::layout::Rect& r = ctx.rect();
  const double listH = r.h + range;
  const double thumbH = std::clamp(r.h * r.h / listH, kMinThumb, static_cast<double>(r.h));
  const double thumbY = r.y + (scroll_ / range) * (r.h - thumbH);
  const render::Rect thumb = ctx.toPhysical(r.x + r.w - kScrollBarWidth - kScrollBarInset, thumbY, kScrollBarWidth, thumbH);
  ctx.painter().fillRoundedRect(thumb, render::CornerRadii::uniform(ctx.px(kScrollBarWidth * 0.5)), ctx.color("border-strong"));
}

// ---- keyboard -----------------------------------------------------------------------------------

void MenuPanel::onPointerEnter(Event&) {
  if (listener_ != nullptr) listener_->panelPointerEntered(*this);
}

void MenuPanel::onPointerMove(Event& e) {
  if (listener_ != nullptr) listener_->panelPointerMoved(*this, e.x);
}

void MenuPanel::onKeyDown(Event& e) {
  if ((e.modifiers & (events::Mod::kCtrl | events::Mod::kAlt | events::Mod::kMeta)) != 0) return;
  switch (e.key) {
    case events::Key::Down: moveHighlight(1); e.markHandled(); return;
    case events::Key::Up: moveHighlight(-1); e.markHandled(); return;
    case events::Key::Home: highlightFirst(); e.markHandled(); return;
    case events::Key::End: highlightLast(); e.markHandled(); return;
    case events::Key::PageDown:
    case events::Key::PageUp: {
      // A page is the number of rows that fit; at least one.
      const core::layout::Rect panel = ui().absRect(id());
      const core::layout::Rect row = ui().absRect(itemWidget(std::max(0, highlighted_)));
      const int step = row.h > 0 ? std::max(1, static_cast<int>(panel.h / row.h) - 1) : 1;
      for (int i = 0; i < step; ++i) {
        if (!moveHighlight(e.key == events::Key::PageDown ? 1 : -1)) break;
      }
      e.markHandled();
      return;
    }
    case events::Key::Enter:
    case events::Key::Space: {
      if (e.key == events::Key::Space && !prefix_.empty() && e.timestampMs - prefixAtMs_ < kPrefixResetMs) {
        prefix_.push_back(' ');  // a space inside a type-ahead prefix, not an activation
        prefixAtMs_ = e.timestampMs;
        jumpToPrefix();
        e.markHandled();
        return;
      }
      e.markHandled();
      if (highlighted_ >= 0) itemActivated(highlighted_, true);
      return;
    }
    case events::Key::Right:
      if (listener_ != nullptr && listener_->panelNavigationKey(*this, true)) e.markHandled();
      return;
    case events::Key::Left:
      if (listener_ != nullptr && listener_->panelNavigationKey(*this, false)) e.markHandled();
      return;
    case events::Key::Tab:
      e.markHandled();
      if (listener_ != nullptr) listener_->panelDismissRequested(*this);
      return;
    default: return;
  }
}

void MenuPanel::onTextInput(Event& e) {
  if ((e.modifiers & (events::Mod::kCtrl | events::Mod::kAlt | events::Mod::kMeta)) != 0) return;
  if (e.codePoint <= U' ' || e.codePoint == 0x7F) return;  // controls and the space (handled as a key)
  if (e.timestampMs - prefixAtMs_ >= kPrefixResetMs || e.timestampMs < prefixAtMs_) prefix_.clear();
  prefixAtMs_ = e.timestampMs;
  appendUtf8(prefix_, e.codePoint);
  jumpToPrefix();
  e.markHandled();
}

void MenuPanel::jumpToPrefix() {
  if (prefix_.empty() || items_.empty()) return;
  // One typed character moves on from the current row (repeat to cycle); a longer prefix refines it.
  const bool single = prefix_.size() == 1 || std::all_of(prefix_.begin(), prefix_.end(), [&](char c) { return c == prefix_[0]; });
  const int count = itemCount();
  const int start = highlighted_ >= 0 ? highlighted_ + (single ? 1 : 0) : 0;
  const std::string needle = single ? prefix_.substr(0, 1) : prefix_;
  for (int step = 0; step < count; ++step) {
    const int i = (start + step) % count;
    if (selectable(i) && startsWithFolded(items_[static_cast<size_t>(i)].label, needle)) {
      setHighlight(i);
      return;
    }
  }
}

}  // namespace r1ui::widgets
