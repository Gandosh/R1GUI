// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of MenuItems.h: measuring and painting menu rows and their style rows.
// Invariants: measure() and paint() use the same font sizes (style size x display scale), so a row
//   that fits its measured width never truncates; the row never mutates the tree while painting.
// Callers: MenuPanel (creation), UiContext (measure and paint dispatch).
#include "r1ui/widgets/menu/MenuItems.h"

#include <algorithm>
#include <cmath>

#include "r1ui/widgets/menu/MenuPanel.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace {

using theme::State::kDisabled;
using theme::State::kHover;
using theme::State::kNone;
using theme::StyleProperty;

constexpr theme::StyleRuleEntry kRows[] = {
    {"menu.item", kNone, StyleProperty::Background, "transparent"},
    {"menu.item", kNone, StyleProperty::Foreground, "color:surface"},
    {"menu.item", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"menu.item", kNone, StyleProperty::LineHeight, "number:16"},
    {"menu.item", kNone, StyleProperty::PaddingX, "metric:menu.itemPadX"},
    {"menu.item", kNone, StyleProperty::PaddingY, "metric:menu.itemPadY"},
    {"menu.item", kNone, StyleProperty::Radius, "metric:menu.itemRadius"},
    {"menu.item", kHover, StyleProperty::Background, "color:hover"},
    {"menu.item", kDisabled, StyleProperty::Foreground, "color:muted@0.5"},
    {"menu.item.shortcut", kNone, StyleProperty::Foreground, "color:muted"},
    {"menu.item.shortcut", kNone, StyleProperty::FontSize, "fontSize:11"},
    {"menu.item.shortcut", kNone, StyleProperty::LineHeight, "number:16"},
    {"menu.item.shortcut", kDisabled, StyleProperty::Foreground, "color:muted@0.5"},
    {"menu.item.icon", kNone, StyleProperty::Foreground, "color:muted"},
    {"menu.item.icon", kDisabled, StyleProperty::Foreground, "color:muted@0.5"},
    {"menu.item.description", kNone, StyleProperty::Foreground, "color:muted"},
    {"menu.item.description", kNone, StyleProperty::FontSize, "fontSize:11"},
    {"menu.item.description", kNone, StyleProperty::LineHeight, "number:13.333"},
    {"menu.item.description", kDisabled, StyleProperty::Foreground, "color:muted@0.5"},
    // Component tone: component colour for text, shortcut and icon, 12% highlight (spec 2.9).
    {"menu.item.component", kNone, StyleProperty::Background, "transparent"},
    {"menu.item.component", kNone, StyleProperty::Foreground, "color:component"},
    {"menu.item.component", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"menu.item.component", kNone, StyleProperty::LineHeight, "number:16"},
    {"menu.item.component", kNone, StyleProperty::PaddingX, "metric:menu.itemPadX"},
    {"menu.item.component", kNone, StyleProperty::PaddingY, "metric:menu.itemPadY"},
    {"menu.item.component", kNone, StyleProperty::Radius, "metric:menu.itemRadius"},
    {"menu.item.component", kHover, StyleProperty::Background, "color:component@0.12"},
    {"menu.item.component", kDisabled, StyleProperty::Foreground, "color:component@0.4"},
    {"menu.item.component.shortcut", kNone, StyleProperty::Foreground, "color:component@0.6"},
    {"menu.item.component.shortcut", kNone, StyleProperty::FontSize, "fontSize:11"},
    {"menu.item.component.shortcut", kNone, StyleProperty::LineHeight, "number:16"},
    {"menu.item.component.shortcut", kDisabled, StyleProperty::Foreground, "color:component@0.4"},
    {"menu.item.component.icon", kNone, StyleProperty::Foreground, "color:component"},
    {"menu.item.component.icon", kDisabled, StyleProperty::Foreground, "color:component@0.4"},
    {"menu.separator", kNone, StyleProperty::Background, "color:border"},
    {"menu.heading", kNone, StyleProperty::Foreground, "color:muted"},
    {"menu.heading", kNone, StyleProperty::FontSize, "fontSize:10"},
    {"menu.heading", kNone, StyleProperty::LineHeight, "number:14"},
    {"menu.heading", kNone, StyleProperty::PaddingX, "metric:menu.itemPadX"},
};

// The arrow of submenu rows in context menus (U+203A single right-pointing angle quotation mark).
constexpr const char* kArrowGlyph = "\xE2\x80\xBA";

double textWidth(UiContext& ui, std::string_view text, double fontSize, int weight) {
  if (text.empty()) return 0.0;
  const double scale = ui.scale();
  return static_cast<double>(ui.text().measure(text, static_cast<float>(fontSize * scale), weight)) / scale;
}

}  // namespace

// ---- MenuItemWidget -----------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> MenuItemWidget::styleRows() { return kRows; }

MenuItemWidget::MenuItemWidget(const MenuItemSpec& spec, core::tree::WidgetId owner, int index, const MenuLook& look)
    : spec_(spec), owner_(owner), index_(index), look_(look) {}

const char* MenuItemWidget::itemKey() const { return spec_.tone == MenuTone::Component ? "menu.item.component" : "menu.item"; }
const char* MenuItemWidget::shortcutKey() const { return spec_.tone == MenuTone::Component ? "menu.item.component.shortcut" : "menu.item.shortcut"; }
const char* MenuItemWidget::iconKey() const { return spec_.tone == MenuTone::Component ? "menu.item.component.icon" : "menu.item.icon"; }

void MenuItemWidget::onAttached() {
  core::layout::Style& s = style();
  s.hasMeasure = true;
  s.flexShrink = 0.0;
  if (!spec_.enabled) setEnabled(false);
  if (!spec_.tooltip.empty()) {
    setTooltip(tooltipWithShortcut(spec_.tooltip, spec_.shortcut));
  }
}

std::string_view MenuItemWidget::accessibleName() const { return WidgetObject::accessibleName().empty() ? std::string_view(spec_.label) : WidgetObject::accessibleName(); }

uint8_t MenuItemWidget::styleState() const {
  uint8_t bits = WidgetObject::styleState();
  if (highlighted_) bits = static_cast<uint8_t>(bits | theme::State::kHover);
  return bits;
}

void MenuItemWidget::setHighlighted(bool on) {
  if (on == highlighted_) return;
  highlighted_ = on;
  requestPaint();
}

void MenuItemWidget::setChecked(bool checked) {
  if (spec_.checked == checked) return;
  spec_.checked = checked;
  requestPaint();
}

double MenuItemWidget::rightCellWidth() const {
  switch (spec_.kind) {
    case MenuItemKind::Submenu: return look_.arrowGlyph ? textWidth(ui(), kArrowGlyph, MenuRowMetrics::kArrowGlyphSize, 400) : MenuRowMetrics::kChevronCell;
    case MenuItemKind::Check: return MenuRowMetrics::kCheckSize;
    case MenuItemKind::Radio: return MenuRowMetrics::kCheckSize;
    default: return 0.0;
  }
}

core::layout::MeasureResult MenuItemWidget::measure(const core::layout::MeasureInput& input) {
  UiContext& u = ui();
  const theme::ResolvedStyle& rs = u.services().resolve(itemKey(), 0);
  double width = 2.0 * rs.paddingX;
  if (!spec_.icon.empty()) width += look_.iconSize + look_.iconGap;
  width += textWidth(u, spec_.label, rs.text.fontSize, rs.text.weight);
  if (!spec_.description.empty()) {
    const theme::ResolvedStyle& ds = u.services().resolve("menu.item.description", 0);
    width = std::max(width, 2.0 * rs.paddingX + textWidth(u, spec_.description, ds.text.fontSize, ds.text.weight) + (spec_.icon.empty() ? 0.0 : look_.iconSize + look_.iconGap));
  }
  if (!spec_.shortcut.empty()) {
    const theme::ResolvedStyle& ss = u.services().resolve(shortcutKey(), 0);
    width += MenuRowMetrics::kMinGap + textWidth(u, spec_.shortcut, ss.text.fontSize, ss.text.weight);
  }
  if (rightCellWidth() > 0.0) width += MenuRowMetrics::kMinGap + rightCellWidth();
  double content = rs.text.lineHeight;
  if (!spec_.description.empty()) content += u.services().resolve("menu.item.description", 0).text.lineHeight;
  if (spec_.kind == MenuItemKind::Submenu && look_.arrowGlyph) content = std::max(content, MenuRowMetrics::kArrowGlyphLine);
  if (input.widthMode == core::layout::MeasureMode::AtMost) width = std::min(width, input.width);
  return {width, content + 2.0 * rs.paddingY};
}

void MenuItemWidget::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.style(itemKey());
  ctx.fillBox(rs);
  const render::Rect box = ctx.box();
  const double padX = rs.paddingX;
  const double padY = rs.paddingY;
  double left = padX;
  if (!spec_.icon.empty()) {
    const theme::ResolvedStyle& is = ctx.style(iconKey());
    const render::Rect iconBox{box.x + ctx.px(left), box.y, ctx.px(look_.iconSize), box.h};
    ctx.drawIcon(spec_.icon, look_.iconSize, iconBox, ctx.color(is.text.color));
    left += look_.iconSize + look_.iconGap;
  }

  // Right side, from the right padding inwards: the cell (chevron, check mark or radio dot), then the
  // shortcut text; `edge` ends as the limit the label may reach (a 24 px gap before what is right of it).
  double edge = ctx.rect().w - padX;
  const double cell = rightCellWidth();
  if (cell > 0.0) {
    const render::Rect cellBox{box.x + ctx.px(edge - cell), box.y, ctx.px(cell), box.h};
    if (spec_.kind == MenuItemKind::Submenu) {
      const render::Color arrowTint = ctx.color(ctx.style(iconKey()).text.color);
      if (look_.arrowGlyph) {
        theme::TextStyle arrow = ctx.style(shortcutKey()).text;
        arrow.fontSize = MenuRowMetrics::kArrowGlyphSize;
        TextOptions arrowOptions;
        arrowOptions.ellipsis = false;
        arrowOptions.color = arrowTint;
        const render::Rect line{cellBox.x, box.y + ctx.px(padY), cellBox.w + 1.0f, ctx.px(MenuRowMetrics::kArrowGlyphLine)};
        ctx.drawText(kArrowGlyph, arrow, line, arrowOptions);
      } else {
        ctx.drawIcon("chevron-right", MenuRowMetrics::kChevronCell, cellBox, arrowTint);
      }
    } else if (spec_.checked && spec_.kind == MenuItemKind::Check) {
      ctx.drawIcon("check", MenuRowMetrics::kCheckSize, cellBox, ctx.color(rs.text.color));
    } else if (spec_.checked && spec_.kind == MenuItemKind::Radio) {
      const float dot = ctx.px(6.0);
      const render::Rect dotBox{cellBox.x + (cellBox.w - dot) * 0.5f, cellBox.y + (cellBox.h - dot) * 0.5f, dot, dot};
      ctx.painter().fillRoundedRect(dotBox, render::CornerRadii::uniform(dot * 0.5f), ctx.color(rs.text.color));
    }
    edge -= cell + MenuRowMetrics::kMinGap;
  }
  if (!spec_.shortcut.empty()) {
    const theme::ResolvedStyle& ss = ctx.style(shortcutKey());
    // The shortcut sits right of the cell gap; with no cell it ends at the right padding.
    const double end = cell > 0.0 ? edge + MenuRowMetrics::kMinGap : edge;
    const double width = std::min(textWidth(ctx.ui(), spec_.shortcut, ss.text.fontSize, ss.text.weight), std::max(0.0, end - left));
    const render::Rect placed{box.x + ctx.px(end - width), box.y + ctx.px(padY), ctx.px(width) + 1.0f, ctx.px(rs.text.lineHeight)};
    TextOptions o;
    o.ellipsis = false;
    ctx.drawText(spec_.shortcut, ss.text, placed, o);
    edge = end - width - MenuRowMetrics::kMinGap;
  }
  const render::Rect labelBox{box.x + ctx.px(left), box.y + ctx.px(padY), ctx.px(std::max(0.0, edge - left) + 1.0), ctx.px(rs.text.lineHeight)};  // +1 px: the row width is the measured width rounded down
  TextOptions options;
  options.ellipsis = true;
  ctx.drawText(spec_.label, rs.text, labelBox, options);
  if (!spec_.description.empty()) {
    const theme::ResolvedStyle& ds = ctx.style("menu.item.description");
    const render::Rect descBox{labelBox.x, labelBox.y + labelBox.h, labelBox.w, ctx.px(ds.text.lineHeight)};
    ctx.drawText(spec_.description, ds.text, descBox, options);
  }
}

void MenuItemWidget::onPointerMove(Event&) {
  if (MenuPanel* panel = ui().objectAs<MenuPanel>(owner_)) panel->itemPointerMoved(index_);
}

void MenuItemWidget::onPointerLeave(Event&) {
  if (MenuPanel* panel = ui().objectAs<MenuPanel>(owner_)) panel->itemPointerLeft(index_);
}

void MenuItemWidget::onClick(Event& e) {
  e.markHandled();
  if (MenuPanel* panel = ui().objectAs<MenuPanel>(owner_)) panel->itemActivated(index_, false);
}

// ---- MenuSeparatorWidget --------------------------------------------------------------------------

void MenuSeparatorWidget::onAttached() {
  core::layout::Style& s = style();
  s.hasMeasure = true;
  s.flexShrink = 0.0;
  s.margin[core::layout::kLeft] = core::layout::Length::px(MenuRowMetrics::kSeparatorMargin);
  s.margin[core::layout::kRight] = core::layout::Length::px(MenuRowMetrics::kSeparatorMargin);
  s.margin[core::layout::kTop] = core::layout::Length::px(MenuRowMetrics::kSeparatorMargin);
  s.margin[core::layout::kBottom] = core::layout::Length::px(MenuRowMetrics::kSeparatorMargin);
  node().flags.hitTestTransparent = true;
}

core::layout::MeasureResult MenuSeparatorWidget::measure(const core::layout::MeasureInput&) { return {0.0, MenuRowMetrics::kSeparatorHeight}; }

void MenuSeparatorWidget::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.resolve("menu.separator", 0);
  ctx.painter().fillRect(ctx.box(), ctx.color(rs.background));
}

// ---- MenuHeadingWidget ----------------------------------------------------------------------------

void MenuHeadingWidget::onAttached() {
  core::layout::Style& s = style();
  s.hasMeasure = true;
  s.flexShrink = 0.0;
  node().flags.hitTestTransparent = true;
}

core::layout::MeasureResult MenuHeadingWidget::measure(const core::layout::MeasureInput& input) {
  const theme::ResolvedStyle& rs = ui().services().resolve("menu.heading", 0);
  double width = 2.0 * rs.paddingX + textWidth(ui(), label_, rs.text.fontSize, rs.text.weight);
  if (input.widthMode == core::layout::MeasureMode::AtMost) width = std::min(width, input.width);
  return {width, rs.text.lineHeight + 8.0};  // 6 px above and 2 px below the label
}

void MenuHeadingWidget::paint(PaintContext& ctx) {
  const theme::ResolvedStyle& rs = ctx.resolve("menu.heading", 0);
  const render::Rect box = ctx.box();
  const render::Rect line{box.x, box.y + ctx.px(6.0), box.w, ctx.px(rs.text.lineHeight)};
  TextOptions o;
  o.padLeft = rs.paddingX;
  o.padRight = rs.paddingX;
  ctx.drawText(label_, rs.text, line, o);
}

}  // namespace r1ui::widgets
