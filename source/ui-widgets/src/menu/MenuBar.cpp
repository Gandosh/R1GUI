// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of MenuBar.h.
// Invariants: open_ is -1 or the index of the title whose menu is the root of the controller's
//   stack; the open title carries the Selected flag; open_ is reset whenever the stack closes.
// Callers: UiContext (widget dispatch), tests, the gallery.
#include "r1ui/widgets/menu/MenuBar.h"

#include <algorithm>

#include "r1ui/widgets/menu/MenuItems.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace events = core::events;
using core::tree::WidgetId;

namespace {

using theme::State::kHover;
using theme::State::kNone;
using theme::State::kSelected;
using theme::StyleProperty;

constexpr theme::StyleRuleEntry kRows[] = {
    {"menubar.item", kNone, StyleProperty::Background, "transparent"},
    {"menubar.item", kNone, StyleProperty::Foreground, "color:muted"},
    {"menubar.item", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"menubar.item", kNone, StyleProperty::LineHeight, "number:16"},
    {"menubar.item", kNone, StyleProperty::PaddingX, "number:8"},
    {"menubar.item", kNone, StyleProperty::PaddingY, "number:4"},
    {"menubar.item", kNone, StyleProperty::Radius, "radius:sm"},
    {"menubar.item", kHover, StyleProperty::Background, "color:hover"},
    {"menubar.item", kHover, StyleProperty::Foreground, "color:surface"},
    {"menubar.item", kSelected, StyleProperty::Background, "color:hover"},
    {"menubar.item", kSelected, StyleProperty::Foreground, "color:surface"},
};

// Where a menu opens relative to its title (measured File menu: 5 px below, 1 px left).
constexpr int kMenuOffsetX = -1;
constexpr double kMenuGap = 5.0;
constexpr double kSubmenuGap = 4.0;
constexpr double kMenuMinWidth = 208.0;
constexpr double kSubmenuMinWidth = 174.0;

}  // namespace

// ---- MenuBarItem --------------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> MenuBarItem::styleRows() { return kRows; }

MenuBarItem::MenuBarItem(std::string title, WidgetId bar, int index) : title_(std::move(title)), bar_(bar), index_(index) {}

void MenuBarItem::onAttached() {
  core::layout::Style& s = style();
  s.hasMeasure = true;
  s.flexShrink = 0.0;
  setFocusable(true);
}

uint8_t MenuBarItem::styleState() const {
  uint8_t bits = WidgetObject::styleState();
  bits = static_cast<uint8_t>(bits & ~theme::State::kFocus);  // focus is a ring, not a fill
  return bits;
}

core::layout::MeasureResult MenuBarItem::measure(const core::layout::MeasureInput& input) {
  const theme::ResolvedStyle& rs = ui().services().resolve("menubar.item", 0);
  const double scale = ui().scale();
  double width = 2.0 * rs.paddingX + static_cast<double>(ui().text().measure(title_, static_cast<float>(rs.text.fontSize * scale), rs.text.weight)) / scale;
  if (input.widthMode == core::layout::MeasureMode::AtMost) width = std::min(width, input.width);
  return {width, rs.text.lineHeight + 2.0 * rs.paddingY};
}

void MenuBarItem::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style("menubar.item");
  ctx.fillBox(rs);
  TextOptions o;
  o.padLeft = rs.paddingX;
  o.padRight = rs.paddingX;
  o.ellipsis = false;  // the title fits its measured width; rounding must not shorten it
  const render::Rect box = ctx.box();
  const render::Rect line{box.x, box.y + ctx.px(rs.paddingY), box.w, ctx.px(rs.text.lineHeight)};
  ctx.drawText(title_, rs.text, line, o);
}

void MenuBarItem::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(ctx.resolve("menubar.item", 0).radius));
}

void MenuBarItem::onPointerEnter(Event&) {
  if (MenuBar* bar = ui().objectAs<MenuBar>(bar_)) bar->itemHovered(index_);
}

void MenuBarItem::onClick(Event& e) {
  e.markHandled();
  if (MenuBar* bar = ui().objectAs<MenuBar>(bar_)) bar->itemClicked(index_);
}

void MenuBarItem::onKeyDown(Event& e) {
  if (MenuBar* bar = ui().objectAs<MenuBar>(bar_)) bar->itemKey(index_, e);
}

// ---- MenuBar ------------------------------------------------------------------------------------

void MenuBar::onAttached() {
  core::layout::Style& s = style();
  s.direction = core::layout::FlexDirection::Row;
  s.alignItems = core::layout::Align::Center;
  s.gapColumn = 1.0;  // measured spacing of the titles
  controller_ = std::make_unique<MenuController>(ui());
  controller_->setRootNavigationHandler([this](bool right) {
    const int next = open_ + (right ? 1 : -1);
    return open_ >= 0 && next >= 0 && next < menuCount() && openMenu(next, true);
  });
}

void MenuBar::onDetached() {
  if (controller_) controller_->close();
}

int MenuBar::addMenu(std::string title, MenuSpec menu) {
  title = sanitizeMenuText(title);
  if (title.empty()) return -1;
  if (menu.minWidth <= 0.0) menu.minWidth = kMenuMinWidth;
  if (menu.look.submenuMinWidth <= 0.0) menu.look.submenuMinWidth = kSubmenuMinWidth;
  const int index = static_cast<int>(menus_.size());
  Entry entry{std::move(title), std::move(menu), {}};
  entry.widget = ui().create<MenuBarItem>(id(), entry.title, id(), index).id();
  menus_.push_back(std::move(entry));
  return index;
}

WidgetId MenuBar::itemWidget(int index) const {
  return index >= 0 && index < menuCount() ? menus_[static_cast<size_t>(index)].widget : WidgetId{};
}

void MenuBar::setOpen(int index) {
  if (open_ == index) return;
  if (WidgetObject* old = ui().object(itemWidget(open_))) old->setSelected(false);
  open_ = index;
  if (WidgetObject* now = ui().object(itemWidget(open_))) now->setSelected(true);
}

bool MenuBar::openMenu(int index, bool fromKeyboard) {
  if (index < 0 || index >= menuCount()) return false;
  const Entry& entry = menus_[static_cast<size_t>(index)];
  const core::layout::Rect r = ui().absRect(entry.widget);
  MenuOpenOptions o;
  o.anchor = {r.x + kMenuOffsetX, r.y, r.w, r.h};
  o.placement = Placement::BelowStart;
  o.gap = kMenuGap;
  o.submenuGap = kSubmenuGap;
  o.anchorWidget = entry.widget;
  o.highlightFirst = fromKeyboard;
  // The close callback runs for replaced and dismissed menus alike; it only resets the title that is
  // still marked open (a replaced menu's callback runs before the new title is marked).
  o.onClosed = [this, index]() {
    if (open_ == index) setOpen(-1);
  };
  MenuSpec copy = entry.spec;
  if (beforeOpen_) beforeOpen_(index, copy);
  if (!controller_->open(std::move(copy), o)) return false;  // a replaced menu already reset its title
  setOpen(index);
  return true;
}

void MenuBar::closeMenu() {
  if (controller_) controller_->close();
}

void MenuBar::itemHovered(int index) {
  if (open_ >= 0 && index != open_ && controller_->isOpen()) openMenu(index, false);
}

void MenuBar::itemClicked(int index) {
  if (open_ == index && controller_->isOpen()) {
    controller_->close();
    return;
  }
  openMenu(index, false);
}

bool MenuBar::moveTo(int index, bool openMenuToo) {
  if (index < 0 || index >= menuCount()) return false;
  ui().router().focus(menus_[static_cast<size_t>(index)].widget, events::FocusReason::Keyboard);
  if (openMenuToo) openMenu(index, true);
  return true;
}

void MenuBar::itemKey(int index, Event& e) {
  if ((e.modifiers & (events::Mod::kCtrl | events::Mod::kAlt | events::Mod::kMeta)) != 0) return;
  switch (e.key) {
    case events::Key::Left: moveTo(index - 1, open_ >= 0); e.markHandled(); return;
    case events::Key::Right: moveTo(index + 1, open_ >= 0); e.markHandled(); return;
    case events::Key::Down:
    case events::Key::Enter:
    case events::Key::Space:
      if (!e.repeat || e.key == events::Key::Down) openMenu(index, true);
      e.markHandled();
      return;
    default: return;
  }
}

}  // namespace r1ui::widgets
