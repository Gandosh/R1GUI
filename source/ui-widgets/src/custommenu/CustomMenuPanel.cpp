// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CustomMenuButton and CustomMenuPanel (CustomMenuPanel.h): style rows, building the buttons from
//   the set, the live refresh from the commands, execution and painting.
// Invariants: the buttons always mirror the menu in the set (rebuilt on every change of the set before
//   anything else runs); a missing command is shown, never dropped; predicates that throw count as
//   "not available"; the panel never touches its members after running a command (the command may close
//   the panel); paint only draws.
// Callers: the host's dock, the gallery, tests.
#include "r1ui/widgets/custommenu/CustomMenuPanel.h"

#include <algorithm>
#include <cmath>

#include "r1ui/commands/Text.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/scroll/ScrollArea.h"

namespace r1ui::widgets {

namespace {

namespace cm = commands::custommenu;
using theme::State::kActive;
using theme::State::kDisabled;
using theme::State::kHover;
using theme::State::kNone;
using theme::State::kSelected;
using theme::StyleProperty;

constexpr theme::StyleRuleEntry kRows[] = {
    {"custombtn", kNone, StyleProperty::Background, "color:panel-field"},
    {"custombtn", kNone, StyleProperty::Foreground, "color:surface"},
    {"custombtn", kNone, StyleProperty::BorderColor, "color:border"},
    {"custombtn", kNone, StyleProperty::BorderWidth, "number:1"},
    {"custombtn", kNone, StyleProperty::Radius, "radius:md"},
    {"custombtn", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"custombtn", kNone, StyleProperty::LineHeight, "number:16"},
    {"custombtn", kNone, StyleProperty::PaddingX, "number:8"},
    {"custombtn", kHover, StyleProperty::Background, "color:panel-field-hover"},
    {"custombtn", kActive, StyleProperty::Background, "color:hover"},
    {"custombtn", kSelected, StyleProperty::Background, "color:accent@0.18"},
    {"custombtn", kSelected, StyleProperty::BorderColor, "color:accent"},
};

constexpr double kIconSize = 16.0;
constexpr double kTallIconSize = 24.0;
constexpr double kIconGap = 6.0;
constexpr double kTallThreshold = 56.0;
constexpr double kPanelPadding = 8.0;
constexpr double kGap = 6.0;

// A plain flex container used for the rows of buttons.
class PanelRow final : public WidgetObject {
 public:
  const char* typeName() const override { return "CustomMenuRow"; }
  void onAttached() override {
    style().direction = core::layout::FlexDirection::Row;
    style().alignItems = core::layout::Align::Stretch;
    style().gapColumn = kGap;
    style().flexShrink = 0.0;
  }
};

// Keeps the columns of a short last row aligned.
class RowSpacer final : public WidgetObject {
 public:
  const char* typeName() const override { return "CustomMenuSpacer"; }
  void onAttached() override {
    style().flexGrow = 1.0;
    style().flexBasis = core::layout::Length::px(0);
    style().minWidth = core::layout::Length::px(0);
  }
};

bool safeCall(const std::function<bool()>& predicate, bool fallback) {
  if (!predicate) return fallback;
  try {
    return predicate();
  } catch (...) {
    return false;
  }
}

}  // namespace

// ---- CustomMenuButton -----------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> CustomMenuButton::styleRows() { return kRows; }

void CustomMenuButton::onAttached() {
  Pressable::onAttached();
  core::layout::Style& s = style();
  s.flexGrow = 1.0;
  s.flexShrink = 1.0;
  s.flexBasis = core::layout::Length::px(0);
  s.minWidth = core::layout::Length::px(0);
}

uint8_t CustomMenuButton::styleState() const {
  uint8_t bits = Pressable::styleState();
  bits = static_cast<uint8_t>(bits & ~kSelected & ~kDisabled);
  if (state_.checked) bits |= kSelected;
  return bits;
}

float CustomMenuButton::paintOpacity() const { return state_.available ? 1.0f : 0.5f; }

bool CustomMenuButton::setState(CustomMenuButtonState state) {
  if (state == state_) return false;
  state_ = std::move(state);
  requestPaint();
  return true;
}

void CustomMenuButton::activate() {
  if (!state_.available || !onActivate_) return;
  const auto callback = onActivate_;  // the handler may replace or destroy this button
  const std::string id = commandId_;
  callback(id);
}

void CustomMenuButton::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style("custombtn");
  const render::Rect box = ctx.box();
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(rs.radius));
  ctx.painter().fillRoundedRect(box, radii, ctx.color(rs.background));
  ctx.painter().border(box, radii, ctx.px(state_.checked ? 2.0 : rs.border.width), ctx.color(rs.border.color));

  const render::Color fg = ctx.color(rs.text.color);
  const core::layout::Rect self = ctx.rect();
  const double w = static_cast<double>(self.w);
  const double h = static_cast<double>(self.h);
  const bool hasIcon = !state_.icon.empty();
  const bool hasLabel = state_.showLabel && !state_.label.empty();
  const double scale = ctx.scale();
  const auto textWidth = [&](double room) {
    return std::min(room, static_cast<double>(ctx.ui().text().measure(state_.label, static_cast<float>(rs.text.fontSize * scale), rs.text.weight)) / scale);
  };
  TextOptions options;
  options.color = fg;
  if (h >= kTallThreshold && hasIcon) {
    // Icon above the label, centred.
    const double room = w - 2.0 * rs.paddingX;
    const double labelH = hasLabel ? rs.text.lineHeight : 0.0;
    const double gap = hasLabel ? 4.0 : 0.0;
    const double top = (h - (kTallIconSize + gap + labelH)) * 0.5;
    ctx.drawIcon(state_.icon, kTallIconSize, ctx.toPhysical(self.x + (w - kTallIconSize) * 0.5, self.y + top, kTallIconSize, kTallIconSize), fg, "circle");
    if (hasLabel) {
      const double lw = textWidth(room);
      options.align = TextAlign::Center;
      ctx.drawText(state_.label, rs.text, ctx.toPhysical(self.x + (w - lw) * 0.5 - 1.0, self.y + top + kTallIconSize + gap, lw + 2.0, labelH), options);
    }
    return;
  }
  const double room = w - 2.0 * rs.paddingX;
  const double iconW = hasIcon ? kIconSize : 0.0;
  const double gap = hasIcon && hasLabel ? kIconGap : 0.0;
  const double lw = hasLabel ? textWidth(std::max(0.0, room - iconW - gap)) : 0.0;
  const double group = iconW + gap + lw;
  const double left = self.x + (w - group) * 0.5;
  if (hasIcon) ctx.drawIcon(state_.icon, kIconSize, ctx.toPhysical(left, self.y, kIconSize, h), fg, "circle");
  if (hasLabel) ctx.drawText(state_.label, rs.text, ctx.toPhysical(left + iconW + gap, self.y, lw + 1.0, h), options);
}

void CustomMenuButton::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(ctx.style("custombtn").radius));
}

// ---- CustomMenuPanel ------------------------------------------------------------------------------

CustomMenuPanel::CustomMenuPanel(CommandServices services, CommandUiSync& sync, cm::CustomMenuSet& set, std::string menuId)
    : services_(services), sync_(sync), set_(set), menuId_(std::move(menuId)) {}

void CustomMenuPanel::onAttached() {
  core::layout::Style& s = style();
  s.direction = core::layout::FlexDirection::Column;
  s.alignItems = core::layout::Align::Stretch;
  s.width = core::layout::Length::percent(100.0);
  s.height = core::layout::Length::percent(100.0);
  s.padding[core::layout::kLeft] = s.padding[core::layout::kRight] = s.padding[core::layout::kTop] = s.padding[core::layout::kBottom] = kPanelPadding;
  ScrollArea& scroll = ui().create<ScrollArea>(id());
  scroll.style().flexGrow = 1.0;
  scroll.style().flexShrink = 1.0;
  scroll.style().minHeight = core::layout::Length::px(0);
  scroll_ = scroll.id();
  content_ = scroll.content();
  if (WidgetObject* content = ui().object(content_)) {
    content->style().direction = core::layout::FlexDirection::Column;
    content->style().alignItems = core::layout::Align::Stretch;
    content->style().gapRow = kGap;
  }
  listener_ = set_.subscribe([this] { rebuild(); });
  attachment_ = sync_.attach([this] { refresh(); });
  rebuild();
}

void CustomMenuPanel::onDetached() {
  set_.unsubscribe(listener_);
  listener_ = 0;
  attachment_.reset();
}

bool CustomMenuPanel::menuExists() const { return set_.find(menuId_) != nullptr; }

CustomMenuButton* CustomMenuPanel::button(size_t index) const {
  if (index >= buttons_.size()) return nullptr;
  return const_cast<UiContext&>(ui()).objectAs<CustomMenuButton>(buttons_[index]);
}

CustomMenuButtonState CustomMenuPanel::stateFor(const cm::MenuEntry& entry, bool showLabels) const {
  CustomMenuButtonState state;
  state.showLabel = showLabels;
  const commands::CommandDef* def = services_.registry.find(entry.commandId);
  state.missing = def == nullptr;
  state.label = !entry.label.empty() ? entry.label : (def != nullptr ? def->label : entry.commandId);
  state.icon = !entry.icon.empty() ? entry.icon : (def != nullptr ? def->icon : std::string());
  if (!state.icon.empty() && !commands::isValidIconName(state.icon)) state.icon.clear();
  if (def == nullptr) {
    state.available = false;
    state.tooltip = "Command not available: " + entry.commandId;
    return state;
  }
  state.available = safeCall(def->enabled, true) && safeCall(def->visible, true);
  const bool stateful = def->kind == commands::CommandKind::Toggle || def->kind == commands::CommandKind::Radio;
  state.checked = stateful && safeCall(def->checked, false);
  state.tooltip = commandTooltip(services_, *def, def->tooltip.empty() ? def->description : def->tooltip);
  return state;
}

void CustomMenuPanel::rebuild() {
  for (const core::tree::WidgetId row : rows_) ui().destroy(row);
  rows_.clear();
  buttons_.clear();
  entries_.clear();
  const auto notice = [&](const char* text) {
    Label& label = ui().create<Label>(content_, text, LabelRole::Muted);
    rows_.push_back(label.id());
  };
  const cm::CustomMenu* menu = set_.find(menuId_);
  if (menu == nullptr) {
    notice("This custom menu was deleted.");
    return;
  }
  if (menu->kind != cm::MenuKind::Panel) {
    notice("This is a pie menu: hold the right mouse button to open it.");
    return;
  }
  if (menu->entries.empty()) {
    notice("This menu has no actions yet. Add some in the Create Custom Menu window.");
    return;
  }
  entries_ = menu->entries;
  showLabels_ = menu->panel.showLabels;
  const size_t columns = static_cast<size_t>(std::clamp(menu->panel.columns, cm::kMinColumns, cm::kMaxColumns));
  const double height = static_cast<double>(std::clamp(menu->panel.buttonSize, cm::kMinButtonSize, cm::kMaxButtonSize));
  const auto activate = [this](const std::string& commandId) { activateCommand(commandId); };
  core::tree::WidgetId row;
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (i % columns == 0) {
      row = ui().create<PanelRow>(content_).id();
      rows_.push_back(row);
    }
    CustomMenuButton& b = ui().create<CustomMenuButton>(row, entries_[i].commandId, activate);
    b.style().height = core::layout::Length::px(height);
    b.setState(stateFor(entries_[i], showLabels_));
    buttons_.push_back(b.id());
  }
  for (size_t pad = entries_.size() % columns; pad != 0 && pad < columns; ++pad) {
    RowSpacer& spacer = ui().create<RowSpacer>(row);
    spacer.style().height = core::layout::Length::px(height);
  }
}

void CustomMenuPanel::refresh() {
  for (size_t i = 0; i < buttons_.size() && i < entries_.size(); ++i) {
    if (CustomMenuButton* b = ui().objectAs<CustomMenuButton>(buttons_[i])) b->setState(stateFor(entries_[i], showLabels_));
  }
}

void CustomMenuPanel::activateCommand(const std::string& commandId) {
  UiContext* context = &ui();
  const core::tree::WidgetId self = id();
  const CommandServices services = services_;
  try {
    services.router.execute(commandId, commands::ExecuteSource::Toolbar);
  } catch (...) {
    // The router contains command failures; anything else must still not unwind through the input path.
  }
  if (CustomMenuPanel* panel = context->objectAs<CustomMenuPanel>(self)) panel->refresh();
}

// ---- dock glue ------------------------------------------------------------------------------------

PanelFactory makeCustomMenuPanelFactory(CommandServices services, CommandUiSync& sync, cm::CustomMenuSet& set, std::string menuId) {
  return [services, &sync, &set, menuId = std::move(menuId)](UiContext& ui, core::tree::WidgetId parent) {
    return ui.create<CustomMenuPanel>(parent, services, sync, set, menuId).id();
  };
}

PanelDescriptor describeCustomMenuPanel(const cm::CustomMenu& menu, dock::PanelId panelId, PanelFactory factory) {
  PanelDescriptor d;
  d.id = panelId;
  d.title = menu.name;
  d.icon = "layout-panel-top";
  d.kind = dock::PanelKind::Panel;
  d.canClose = true;
  d.canFloat = true;
  d.minSize = {120.0, 60.0};
  d.floatSize = {menu.panel.width, menu.panel.height};
  d.factory = std::move(factory);
  return d;
}

}  // namespace r1ui::widgets
