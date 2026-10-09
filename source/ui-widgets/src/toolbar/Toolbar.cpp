// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Toolbar.h and the toolbar.* style rows.
// Invariants: exactly zero or one Tool button is active (setActiveTool and toolActivated are the only
//   writers); a flyout group's main button always carries the id and icon of its current entry; the
//   measured geometry (32 px buttons, 12 px trigger, 4 px separator, gap 2, padding 4 + 1 px border)
//   lives in constants; handlers may destroy the toolbar, so nothing is touched after a callback.
// Callers: UiContext (events, paint), tests, application shells.
#include "r1ui/widgets/toolbar/Toolbar.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace layout = core::layout;
namespace State = theme::State;
using core::events::Button;
using core::events::Key;
using theme::StyleProperty;

namespace {

constexpr double kSurfacePadding = 4.0 + 1.0;  // padding 4 plus the border, which the layout does not know

constexpr theme::StyleRuleEntry kRows[] = {
    {"toolbar.surface", State::kNone, StyleProperty::Background, "color:panel"},
    {"toolbar.surface", State::kNone, StyleProperty::BorderColor, "color:border"},
    {"toolbar.surface", State::kNone, StyleProperty::BorderWidth, "number:1"},
    {"toolbar.surface", State::kNone, StyleProperty::Radius, "radius:xl"},

    {"toolbar.button", State::kNone, StyleProperty::Background, "transparent"},
    {"toolbar.button", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"toolbar.button", State::kNone, StyleProperty::Radius, "metric:toolbar.radius"},
    {"toolbar.button", State::kHover, StyleProperty::Background, "color:hover"},
    {"toolbar.button", State::kHover, StyleProperty::Foreground, "color:surface"},
    {"toolbar.button", State::kSelected, StyleProperty::Background, "color:accent"},
    {"toolbar.button", State::kSelected, StyleProperty::Foreground, "#ffffff"},
    {"toolbar.button", State::kDisabled, StyleProperty::Opacity, "number:0.5"},

    {"toolbar.toggle", State::kNone, StyleProperty::Background, "transparent"},
    {"toolbar.toggle", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"toolbar.toggle", State::kNone, StyleProperty::Radius, "radius:md"},
    {"toolbar.toggle", State::kHover, StyleProperty::Background, "color:hover"},
    {"toolbar.toggle", State::kHover, StyleProperty::Foreground, "color:surface"},
    {"toolbar.toggle", State::kSelected, StyleProperty::Background, "color:hover"},
    {"toolbar.toggle", State::kSelected, StyleProperty::Foreground, "color:surface"},
    {"toolbar.toggle", State::kDisabled, StyleProperty::Opacity, "number:0.5"},

    {"toolbar.trigger", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"toolbar.trigger", State::kHover, StyleProperty::Foreground, "color:surface"},
    {"toolbar.trigger", State::kDisabled, StyleProperty::Opacity, "number:0.5"},

    {"toolbar.separator", State::kNone, StyleProperty::Background, "color:border@0.5"},
};

}  // namespace

std::span<const theme::StyleRuleEntry> Toolbar::styleRows() { return kRows; }

// ---- ToolbarButton ------------------------------------------------------------------------------

void ToolbarButton::onAttached() {
  style().width = layout::Length::px(kSize);
  style().height = layout::Length::px(kSize);
  style().flexShrink = 0.0;
  node().flags.focusable = false;  // the toolbar makes buttons focusable when keyboard navigation is on
}

void ToolbarButton::setIcon(std::string icon) {
  if (icon == icon_) return;
  icon_ = std::move(icon);
  requestPaint();
}

std::string_view ToolbarButton::accessibleName() const {
  return WidgetObject::accessibleName().empty() ? std::string_view(tooltipText()) : WidgetObject::accessibleName();
}

uint8_t ToolbarButton::styleState() const {
  uint8_t s = WidgetObject::styleState();
  if (!focusVisible()) s &= static_cast<uint8_t>(~State::kFocus);
  return s;
}

float ToolbarButton::paintOpacity() const { return static_cast<float>(ui().services().resolve(rowKey(), styleState()).opacity); }

void ToolbarButton::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style(rowKey());
  const render::Color bg = ctx.animatedColor(0, ctx.color(rs.background));
  const render::Rect box = ctx.box();
  if (bg.a > 0.0f) ctx.painter().fillRoundedRect(box, render::CornerRadii::uniform(ctx.px(rs.radius)), bg);
  const render::Color tint = ctx.animatedColor(1, ctx.color(rs.text.color));
  if (!icon_.empty()) ctx.drawIcon(icon_, kIconSize, box, tint);
  if (badge_) {
    // Measured: a 6.5 px dot centred 26 px from the left and 6.5 px from the top, with a 1 px ring in the bar's colour.
    const layout::Rect r = ctx.rect();
    const auto dot = [&](double radius, const render::Color& color) {
      const double cx = r.x + 26.0, cy = r.y + 6.5;
      ctx.painter().fillRoundedRect(ctx.toPhysical(cx - radius, cy - radius, 2 * radius, 2 * radius), render::CornerRadii::uniform(ctx.px(radius)), color);
    };
    dot(3.25 + 1.0, ctx.color("panel"));
    dot(3.25, ctx.color("muted"));
  }
}

void ToolbarButton::setBadge(bool on) {
  if (on == badge_) return;
  badge_ = on;
  requestPaint();
}

void ToolbarButton::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(ctx.style(rowKey()).radius));
}

bool ToolbarButton::activate() {
  if (!enabled()) return false;
  auto callback = onActivate_;
  if (callback) callback(*this);
  return true;
}

void ToolbarButton::onClick(Event& e) {
  if (e.button != Button::Left) return;
  e.markHandled();
  activate();
}

void ToolbarButton::onKeyDown(Event& e) {
  if (e.key != Key::Space && e.key != Key::Enter) return;
  if (e.modifiers & (core::events::Mod::kCtrl | core::events::Mod::kAlt | core::events::Mod::kMeta)) return;
  e.markHandled();
  if (e.repeat || keyPressed_) return;
  keyPressed_ = true;
}

void ToolbarButton::onKeyUp(Event& e) {
  if (e.key != Key::Space && e.key != Key::Enter) return;
  if (!keyPressed_) return;
  keyPressed_ = false;
  e.markHandled();
  activate();
}

void ToolbarButton::onFocusOut(Event&) { keyPressed_ = false; }

void ToolbarButton::onStateChanged(uint16_t) {
  if (!enabled()) keyPressed_ = false;
}

// ---- ToolbarTrigger, ToolbarSeparator -----------------------------------------------------------

void ToolbarGroupBox::onAttached() {
  style().direction = vertical_ ? layout::FlexDirection::Column : layout::FlexDirection::Row;
  style().alignItems = layout::Align::Center;
  style().flexShrink = 0.0;
}

void ToolbarTrigger::onAttached() {
  style().width = layout::Length::px(vertical_ ? ToolbarButton::kSize : kWidth);
  style().height = layout::Length::px(vertical_ ? kWidth : ToolbarButton::kSize);
  style().flexShrink = 0.0;
}

void ToolbarTrigger::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style("toolbar.trigger");
  const render::Color tint = ctx.animatedColor(0, ctx.color(rs.text.color));
  ctx.drawIcon(vertical_ ? "chevron-right" : "chevron-down", 12.0, ctx.box(), tint);
}

void ToolbarTrigger::onClick(Event& e) {
  if (e.button != Button::Left) return;
  e.markHandled();
  if (onOpen_ && enabled()) {
    auto callback = onOpen_;
    callback(*this);
  }
}

void ToolbarTrigger::onKeyDown(Event& e) {
  if (e.key != Key::Space && e.key != Key::Enter && e.key != Key::Down) return;
  e.markHandled();
  if (!e.repeat && onOpen_ && enabled()) {
    auto callback = onOpen_;
    callback(*this);
  }
}

void ToolbarSeparator::onAttached() {
  style().width = layout::Length::px(vertical_ ? kLength : kThickness);
  style().height = layout::Length::px(vertical_ ? kThickness : kLength);
  style().flexShrink = 0.0;
  node().flags.hitTestTransparent = true;
}

void ToolbarSeparator::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style("toolbar.separator");
  const layout::Rect r = ctx.rect();
  const double centre = (vertical_ ? r.y + r.h * 0.5 : r.x + r.w * 0.5);
  render::Rect line;
  if (vertical_) line = ctx.toPhysical(r.x, centre - 0.5, r.w, 1.0);
  else line = ctx.toPhysical(centre - 0.5, r.y, 1.0, r.h);
  ctx.painter().fillRect(line, ctx.color(rs.background));
}

// ---- Toolbar ------------------------------------------------------------------------------------

void Toolbar::onAttached() {
  layout::Style& s = style();
  const bool horizontal = orientation_ == ToolbarOrientation::Horizontal;
  s.direction = horizontal ? layout::FlexDirection::Row : layout::FlexDirection::Column;
  s.alignItems = layout::Align::Center;
  s.alignSelf = layout::Align::Start;
  s.flexShrink = 0.0;
  for (double& p : s.padding) p = kSurfacePadding;
  (horizontal ? s.gapColumn : s.gapRow) = kGap;
  flyoutPlacement_ = horizontal ? Placement::AboveStart : Placement::RightStart;
}

void Toolbar::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.resolve("toolbar.surface", 0);
  const render::Rect box = ctx.box();
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(rs.radius));
  const auto layers = ctx.ui().services().tokens().shadow("lg");
  if (layers) {
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

ToolbarButton& Toolbar::makeButton(core::tree::WidgetId parent, std::string id, std::string icon, std::string tooltip, ToolbarButtonKind kind) {
  ToolbarButton& b = ui().create<ToolbarButton>(parent, std::move(id), std::move(icon), kind);
  b.setTooltip(std::move(tooltip));
  b.node().flags.focusable = keyboard_;
  buttons_.push_back(b.id());
  return b;
}

ToolbarButton& Toolbar::addTool(std::string id, std::string icon, std::string tooltip) {
  ToolbarButton& b = makeButton(this->id(), std::move(id), std::move(icon), std::move(tooltip), ToolbarButtonKind::Tool);
  b.setOnActivate([this](ToolbarButton& button) { toolActivated(button); });
  return b;
}

ToolbarButton& Toolbar::addAction(std::string id, std::string icon, std::string tooltip, std::function<void(ToolbarButton&)> onActivate) {
  ToolbarButton& b = makeButton(this->id(), std::move(id), std::move(icon), std::move(tooltip), ToolbarButtonKind::Action);
  b.setOnActivate(std::move(onActivate));
  return b;
}

ToolbarButton& Toolbar::addToggle(std::string id, std::string icon, std::string tooltip, std::function<void(ToolbarButton&)> onToggle) {
  ToolbarButton& b = makeButton(this->id(), std::move(id), std::move(icon), std::move(tooltip), ToolbarButtonKind::Toggle);
  b.setOnActivate([callback = std::move(onToggle)](ToolbarButton& button) {
    button.setActive(!button.active());
    if (callback) callback(button);
  });
  return b;
}

void Toolbar::addSeparator() { ui().create<ToolbarSeparator>(id(), orientation_ == ToolbarOrientation::Vertical); }

Toolbar::Group* Toolbar::groupOf(core::tree::WidgetId main) {
  for (Group& g : groups_) {
    if (g.main == main) return &g;
  }
  return nullptr;
}

ToolbarButton& Toolbar::addToolGroup(std::vector<ToolEntry> entries) {
  if (entries.empty()) return addTool("", "", "");
  const bool vertical = orientation_ == ToolbarOrientation::Vertical;
  ToolbarGroupBox& box = ui().create<ToolbarGroupBox>(id(), vertical);
  const auto tooltipFor = [](const ToolEntry& e) { return e.shortcut.empty() ? e.label : e.label + " (" + e.shortcut + ")"; };
  ToolbarButton& main = makeButton(box.id(), entries[0].id, entries[0].icon, tooltipFor(entries[0]), ToolbarButtonKind::Tool);
  main.setOnActivate([this](ToolbarButton& button) { toolActivated(button); });
  ToolbarTrigger& trigger = ui().create<ToolbarTrigger>(box.id(), vertical);
  trigger.setTooltip("More tools");
  trigger.node().flags.focusable = keyboard_;
  triggers_.push_back(trigger.id());
  Group g;
  g.main = main.id();
  g.box = box.id();
  g.entries = std::move(entries);
  groups_.push_back(std::move(g));
  const core::tree::WidgetId mainId = main.id();
  trigger.setOnOpen([this, mainId](ToolbarTrigger&) {
    for (size_t i = 0; i < groups_.size(); ++i) {
      if (groups_[i].main == mainId) openGroup(i);
    }
  });
  return main;
}

void Toolbar::openGroup(size_t index) {
  if (index >= groups_.size()) return;
  Group& g = groups_[index];
  std::vector<FlyoutItem> items;
  items.reserve(g.entries.size());
  for (const ToolEntry& e : g.entries) {
    FlyoutItem item;
    item.label = e.label;
    item.icon = e.icon;
    item.shortcut = e.shortcut;
    items.push_back(std::move(item));
  }
  FlyoutOpenOptions options;
  options.anchor = ui().absRect(g.box);
  options.placement = flyoutPlacement_;
  options.gap = 8.0;
  options.owner = id();
  options.initialHighlight = static_cast<int>(g.current);
  const core::tree::WidgetId mainId = g.main;
  UiContext* context = &ui();
  const core::tree::WidgetId self = id();
  openFlyout(
      ui(), std::move(items),
      [context, self, mainId](size_t entryIndex) {
        Toolbar* bar = context->objectAs<Toolbar>(self);
        ToolbarButton* main = context->objectAs<ToolbarButton>(mainId);
        if (bar == nullptr || main == nullptr) return;
        Group* group = bar->groupOf(mainId);
        if (group == nullptr || entryIndex >= group->entries.size()) return;
        const ToolEntry& entry = group->entries[entryIndex];
        group->current = entryIndex;
        main->setToolId(entry.id);
        main->setIcon(entry.icon);
        main->setTooltip(entry.shortcut.empty() ? entry.label : entry.label + " (" + entry.shortcut + ")");
        bar->toolActivated(*main);
      },
      options);
}

void Toolbar::toolActivated(ToolbarButton& button) {
  for (const core::tree::WidgetId b : buttons_) {
    if (ToolbarButton* other = ui().objectAs<ToolbarButton>(b)) {
      if (other->kind() == ToolbarButtonKind::Tool) other->setActive(other == &button);
    }
  }
  if (onTool_) {
    const std::string tool = button.toolId();
    auto callback = onTool_;
    callback(tool);
  }
}

bool Toolbar::setActiveTool(std::string_view toolId) {
  ToolbarButton* match = nullptr;
  for (const core::tree::WidgetId b : buttons_) {
    ToolbarButton* button = ui().objectAs<ToolbarButton>(b);
    if (button != nullptr && button->kind() == ToolbarButtonKind::Tool && button->toolId() == toolId) {
      match = button;
      break;
    }
  }
  if (match == nullptr) {
    // A non-current entry of a flyout group: make it the group's tool.
    for (Group& g : groups_) {
      for (size_t i = 0; i < g.entries.size() && match == nullptr; ++i) {
        if (g.entries[i].id != toolId) continue;
        if (ToolbarButton* main = ui().objectAs<ToolbarButton>(g.main)) {
          g.current = i;
          main->setToolId(g.entries[i].id);
          main->setIcon(g.entries[i].icon);
          main->setTooltip(g.entries[i].shortcut.empty() ? g.entries[i].label : g.entries[i].label + " (" + g.entries[i].shortcut + ")");
          match = main;
        }
      }
    }
  }
  if (match == nullptr) return false;
  for (const core::tree::WidgetId b : buttons_) {
    if (ToolbarButton* other = ui().objectAs<ToolbarButton>(b)) {
      if (other->kind() == ToolbarButtonKind::Tool) other->setActive(other == match);
    }
  }
  return true;
}

std::string Toolbar::activeTool() const {
  for (const core::tree::WidgetId b : buttons_) {
    const ToolbarButton* button = ui().objectAs<ToolbarButton>(b);
    if (button != nullptr && button->kind() == ToolbarButtonKind::Tool && button->active()) return button->toolId();
  }
  return {};
}

ToolbarButton* Toolbar::button(size_t index) { return index < buttons_.size() ? ui().objectAs<ToolbarButton>(buttons_[index]) : nullptr; }

// ---- keyboard -----------------------------------------------------------------------------------

void Toolbar::setKeyboardNavigation(bool enabled) {
  keyboard_ = enabled;
  for (const core::tree::WidgetId b : buttons_) {
    if (core::tree::Widget* w = ui().tree().get(b)) w->flags.focusable = enabled;
  }
  for (const core::tree::WidgetId t : triggers_) {
    if (core::tree::Widget* w = ui().tree().get(t)) w->flags.focusable = enabled;
  }
}

std::vector<core::tree::WidgetId> Toolbar::focusOrder() const {
  std::vector<core::tree::WidgetId> order;
  ui().tree().forEachDescendant(id(), [&](core::tree::WidgetId c) {
    const WidgetObject* o = ui().object(c);
    const bool candidate = dynamic_cast<const ToolbarButton*>(o) != nullptr || dynamic_cast<const ToolbarTrigger*>(o) != nullptr;
    if (candidate && o->enabled()) order.push_back(c);
  });
  return order;
}

void Toolbar::onKeyDown(Event& e) {
  if (!keyboard_) return;
  if (e.modifiers & (core::events::Mod::kCtrl | core::events::Mod::kAlt | core::events::Mod::kMeta)) return;
  const bool horizontal = orientation_ == ToolbarOrientation::Horizontal;
  const Key back = horizontal ? Key::Left : Key::Up;
  const Key forward = horizontal ? Key::Right : Key::Down;
  if (e.key != back && e.key != forward && e.key != Key::Home && e.key != Key::End) return;
  const std::vector<core::tree::WidgetId> order = focusOrder();
  if (order.empty()) return;
  const core::tree::WidgetId focused = ui().router().focused();
  size_t at = order.size();
  for (size_t i = 0; i < order.size(); ++i) {
    if (order[i] == focused) at = i;
  }
  size_t next = at;
  if (e.key == Key::Home) next = 0;
  else if (e.key == Key::End) next = order.size() - 1;
  else if (e.key == forward) next = at == order.size() ? 0 : std::min(at + 1, order.size() - 1);
  else next = at == order.size() || at == 0 ? 0 : at - 1;
  e.markHandled();
  if (next != at) ui().router().focus(order[next], core::events::FocusReason::Keyboard);
}

}  // namespace r1ui::widgets
