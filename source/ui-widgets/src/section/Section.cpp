// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Section.h (PropertySection, PanelHeader, FieldGroup, FieldGrid and their
//   small leaf widgets) and the section.* style rows.
// Invariants: the measured geometry lives here as named constants (header 26, panel header 43, side
//   padding 12, action 26, label gap 4, grid gap 6) with the catalogue numbers they come from; no
//   colour is hard-coded.
// Callers: UiContext (layout, paint, events).
#include "r1ui/widgets/section/Section.h"

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

constexpr double kHeaderHeight = 26.0;      // docs/spec/widgets.md section 3: section header row
constexpr double kSideInset = 12.0;         // panel horizontal padding
constexpr double kContentBottom = 8.0;      // measured: the Layout section is 60 px = 26 (header, its 1 px border included) + 26 (one row) + 8
constexpr double kRowGap = 6.0;
constexpr double kActionHeaderExtra = 8.0;  // measured: add / eye buttons sit 9 px below the separator
constexpr double kPanelHeaderHeight = 43.0; // 8 + 26 + 8 + 1 px bottom border
constexpr double kPanelHeaderPadY = 8.0;
constexpr double kActionSize = 26.0;
constexpr double kLabelGap = 4.0;

constexpr theme::StyleRuleEntry kRows[] = {
    {"section.action", State::kNone, StyleProperty::Background, "transparent"},
    {"section.action", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"section.action", State::kNone, StyleProperty::BorderColor, "transparent"},
    {"section.action", State::kNone, StyleProperty::BorderWidth, "number:1"},
    {"section.action", State::kNone, StyleProperty::Radius, "metric:iconButton.md.radius"},
    {"section.action", State::kHover, StyleProperty::Background, "color:hover"},
    {"section.action", State::kHover, StyleProperty::Foreground, "color:surface"},
    {"section.action", State::kActive, StyleProperty::BorderColor, "color:accent"},
    {"section.action", State::kActive, StyleProperty::Foreground, "color:accent"},
    {"section.action", State::kSelected, StyleProperty::Foreground, "color:accent"},
    {"section.action", State::kDisabled, StyleProperty::Opacity, "number:0.5"},

    {"section.panelTitle", State::kNone, StyleProperty::Foreground, "color:surface"},
    {"section.panelTitle", State::kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"section.panelTitle", State::kNone, StyleProperty::LineHeight, "number:16"},
    {"section.panelTitle", State::kNone, StyleProperty::FontWeight, "weight:semibold"},
    {"section.panelTitle", State::kDisabled, StyleProperty::Opacity, "number:0.5"},

    {"section.panelIcon", State::kNone, StyleProperty::Foreground, "color:muted"},
};

}  // namespace

// ---- leaves -------------------------------------------------------------------------------------

void SectionBox::onAttached() {
  style().direction = layout::FlexDirection::Column;
}

void SectionText::onAttached() {
  style().hasMeasure = true;
  style().flexShrink = 1.0;
  node().flags.hitTestTransparent = true;
}

void SectionText::setText(std::string text) {
  if (text == text_) return;
  text_ = std::move(text);
  requestLayout();
  requestPaint();
}

layout::MeasureResult SectionText::measure(const layout::MeasureInput& input) {
  const theme::ResolvedStyle& rs = ui().services().resolve(key_, styleState());
  const double scale = ui().scale();
  double width = static_cast<double>(ui().text().measure(text_, static_cast<float>(rs.text.fontSize * scale), rs.text.weight)) / scale;
  if (input.widthMode == layout::MeasureMode::AtMost) width = std::min(width, input.width);
  return {width, rs.text.lineHeight};
}

void SectionText::paint(PaintContext& ctx) {
  if (text_.empty()) return;
  ctx.drawText(text_, ctx.style(key_).text, ctx.box());
}

void SectionIcon::onAttached() {
  style().width = layout::Length::px(size_);
  style().height = layout::Length::px(size_);
  style().flexShrink = 0.0;
  node().flags.hitTestTransparent = true;
}

void SectionIcon::setIcon(std::string icon) {
  if (icon == icon_) return;
  icon_ = std::move(icon);
  requestPaint();
}

void SectionIcon::paint(PaintContext& ctx) {
  if (icon_.empty()) return;
  ctx.drawIcon(icon_, size_, ctx.box(), ctx.color(ctx.style(key_).text.color));
}

// ---- header row ---------------------------------------------------------------------------------

void SectionHeader::onAttached() {
  layout::Style& s = style();
  s.direction = layout::FlexDirection::Row;
  s.alignItems = layout::Align::Center;
  s.height = layout::Length::px(kHeaderHeight);
  s.padding[layout::kLeft] = kSideInset;
  s.padding[layout::kRight] = kSideInset;
  s.flexShrink = 0.0;
}

void SectionHeader::setCollapsible(bool collapsible) {
  collapsible_ = collapsible;
  setFocusable(collapsible);
}

void SectionHeader::paintOver(PaintContext& ctx) {
  if (collapsible_ && focusVisible()) ctx.focusRing(0.0f);
}

void SectionHeader::onClick(Event& e) {
  // A click a child already used (a header action button) must not also toggle the section.
  if (!collapsible_ || e.button != Button::Left || !onToggle_ || e.handled) return;
  e.markHandled();
  onToggle_();
}

void SectionHeader::onKeyDown(Event& e) {
  if (!collapsible_ || (e.key != Key::Space && e.key != Key::Enter) || e.target != id()) return;
  e.markHandled();
  if (!e.repeat) keyDown_ = true;
}

void SectionHeader::onKeyUp(Event& e) {
  if (!collapsible_ || (e.key != Key::Space && e.key != Key::Enter) || e.target != id() || !keyDown_) return;
  keyDown_ = false;
  e.markHandled();
  if (onToggle_) onToggle_();
}

// ---- PropertySection ----------------------------------------------------------------------------

std::span<const theme::StyleRuleEntry> PropertySection::styleRows() { return kRows; }

void PropertySection::onAttached() {
  layout::Style& s = style();
  s.direction = layout::FlexDirection::Column;
  s.alignItems = layout::Align::Stretch;

  SectionHeader& header = ui().create<SectionHeader>(id());
  // The separator is part of the 26 px header; the title sits one more pixel down (measured: its text box starts 9 px below the separator).
  header.style().padding[layout::kTop] = options_.topBorder ? 2.0 : 1.0;
  header_ = header.id();
  header.setCollapsible(options_.collapsible);
  header.setOnToggle([this] { toggle(); });
  header.setAccessibleName(title_);
  Label& label = ui().create<Label>(header_, title_, LabelRole::Heading);
  titleLabel_ = label.id();
  SectionBox& spacer = ui().create<SectionBox>(header_);
  spacer.style().flexGrow = 1.0;
  spacer.node().flags.hitTestTransparent = true;
  spacer_ = spacer.id();

  SectionBox& content = ui().create<SectionBox>(id());
  content.style().padding[layout::kLeft] = kSideInset;
  content.style().padding[layout::kRight] = kSideInset;
  content.style().padding[layout::kBottom] = kContentBottom;
  content.style().gapRow = kRowGap;
  content_ = content.id();
}

void PropertySection::paint(PaintContext& ctx) {
  if (!options_.topBorder) return;
  const float line = ctx.hairline();
  const render::Rect box = ctx.box();
  ctx.painter().fillRect({box.x, box.y, box.w, line}, ctx.color("border"));
}

void PropertySection::setTitle(std::string title) {
  if (title == title_) return;
  title_ = std::move(title);
  if (Label* l = ui().objectAs<Label>(titleLabel_)) l->setText(title_);
  if (WidgetObject* h = ui().object(header_)) h->setAccessibleName(title_);
}

ActionButton& PropertySection::addAction(std::string icon, std::string tooltip, std::function<void(ActionButton&)> onActivate) {
  // A header with trailing buttons is taller: 8 px above the 26 px buttons (measured: Fill and Appearance), and
  // the content starts 6 px below it.
  if (core::tree::Widget* h = ui().tree().get(header_)) {
    h->style.height = layout::Length::px((options_.topBorder ? 1.0 : 0.0) + kActionHeaderExtra + kActionSize);
    h->style.padding[layout::kTop] = (options_.topBorder ? 1.0 : 0.0) + kActionHeaderExtra;
    ui().invalidator().requestLayout(header_);
  }
  if (core::tree::Widget* c = ui().tree().get(content_)) {
    c->style.padding[layout::kTop] = kRowGap;
    ui().invalidator().requestLayout(content_);
  }
  ActionButton& b = ui().create<ActionButton>(header_, std::move(icon), "section.action");
  b.setSize(kActionSize, kActionSize);
  b.setTooltip(std::move(tooltip));
  b.setOnActivate(std::move(onActivate));
  return b;
}

bool PropertySection::setCollapsed(bool collapsed) {
  if (!options_.collapsible || collapsed == collapsed_) return false;
  collapsed_ = collapsed;
  if (core::tree::Widget* c = ui().tree().get(content_)) c->style.display = collapsed ? layout::Display::None : layout::Display::Flex;
  ui().invalidator().requestLayout(content_);
  requestLayout();
  if (onToggle_) {
    auto callback = onToggle_;  // the callback may destroy this section
    callback(*this);
  }
  return true;
}

// ---- PanelHeader --------------------------------------------------------------------------------

void PanelHeader::onAttached() {
  layout::Style& s = style();
  s.direction = layout::FlexDirection::Row;
  s.alignItems = layout::Align::Center;
  s.height = layout::Length::px(kPanelHeaderHeight);
  s.flexShrink = 0.0;
  s.padding[layout::kLeft] = kSideInset;
  s.padding[layout::kRight] = kSideInset;
  s.padding[layout::kTop] = kPanelHeaderPadY;
  s.padding[layout::kBottom] = kPanelHeaderPadY + 1.0;
  s.gapColumn = 6.0;
  iconWidget_ = ui().create<SectionIcon>(id(), icon_, "section.panelIcon", 14.0).id();
  SectionText& title = ui().create<SectionText>(id(), title_, "section.panelTitle");
  title.style().flexGrow = 1.0;
  titleWidget_ = title.id();
}

void PanelHeader::paint(PaintContext& ctx) {
  const float line = ctx.hairline();
  const render::Rect box = ctx.box();
  ctx.painter().fillRect({box.x, box.y + box.h - line, box.w, line}, ctx.color("border"));
}

void PanelHeader::setTitle(std::string title) {
  if (title == title_) return;
  title_ = std::move(title);
  if (SectionText* t = ui().objectAs<SectionText>(titleWidget_)) t->setText(title_);
}

void PanelHeader::setIcon(std::string icon) {
  icon_ = icon;
  if (SectionIcon* i = ui().objectAs<SectionIcon>(iconWidget_)) i->setIcon(std::move(icon));
}

ActionButton& PanelHeader::addAction(std::string icon, std::string tooltip, std::function<void(ActionButton&)> onActivate) {
  ActionButton& b = ui().create<ActionButton>(id(), std::move(icon), "section.action");
  b.setSize(kActionSize, kActionSize);
  b.setTooltip(std::move(tooltip));
  b.setOnActivate(std::move(onActivate));
  return b;
}

// ---- FieldGroup / FieldGrid ---------------------------------------------------------------------

void FieldGroup::onAttached() {
  layout::Style& s = style();
  s.direction = layout::FlexDirection::Column;
  s.alignItems = layout::Align::Stretch;
  s.gapRow = kLabelGap;
  s.minWidth = layout::Length::px(0);
  Label& label = ui().create<Label>(id(), label_, LabelRole::Caption);
  label.style().flexShrink = 0.0;  // the group is a column: never squeeze the label's height (its width truncates)
  labelWidget_ = label.id();
  SectionBox& control = ui().create<SectionBox>(id());
  control.style().flexShrink = 0.0;
  control_ = control.id();
}

void FieldGroup::setLabel(std::string label) {
  if (label == label_) return;
  label_ = std::move(label);
  if (Label* l = ui().objectAs<Label>(labelWidget_)) l->setText(label_);
}

void FieldGrid::onAttached() {
  style().direction = layout::FlexDirection::Column;
  style().gapRow = kGap;
}

FieldGrid::Row FieldGrid::addRow(int columns, bool rail) {
  columns = std::clamp(columns, 1, 2);
  Row r;
  SectionBox& row = ui().create<SectionBox>(id());
  row.style().direction = layout::FlexDirection::Row;
  row.style().gapColumn = kGap;
  row.style().alignItems = layout::Align::Stretch;
  r.row = row.id();
  const auto cell = [&] {
    SectionBox& c = ui().create<SectionBox>(r.row);
    c.style().flexGrow = 1.0;
    c.style().flexShrink = 1.0;
    c.style().flexBasis = layout::Length::px(0);
    c.style().minWidth = layout::Length::px(0);
    return c.id();
  };
  r.first = cell();
  if (columns == 2) r.second = cell();
  if (rail) {
    SectionBox& c = ui().create<SectionBox>(r.row);
    c.style().width = layout::Length::px(kRail);
    c.style().flexShrink = 0.0;
    c.style().flexGrow = 0.0;
    r.rail = c.id();
  }
  return r;
}

}  // namespace r1ui::widgets
