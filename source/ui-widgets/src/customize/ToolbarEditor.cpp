// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ToolbarEditor.h, part 1: the strip's model mirror, geometry and painting, and the
//   ToolbarEditor header (size step and gap controls). The strip's pointer, keyboard, context menu and
//   drop logic are in ToolbarEditorInput.cpp.
// Invariants: cells_ mirror Customization::editView() as of the last rebuild() (called on every model or
//   edit-mode notification); the strip's size is always set from the cells, so layout never needs a
//   measure callback; the header controls only reflect the model (changing one calls the model, the
//   notification updates the control).
// Callers: CustomizableToolbar, the gallery, tests.
#include <algorithm>
#include <cmath>

#include "CustomizeBox.h"
#include "CustomizeCommon.h"
#include "ToolbarEditorMetrics.h"
#include "r1ui/widgets/customize/ToolbarEditor.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/segmented/Segmented.h"

namespace r1ui::widgets {

namespace cz = commands::customize;
namespace layout = core::layout;
using core::events::Button;
using core::events::Key;
namespace Mod = core::events::Mod;
using cust::kStripBand;
using cust::kStripPad;
using cust::kStripSeparator;
using cust::kStripSpacer;
using cust::kStripTrigger;
using cust::runStripDrop;

// ---- strip: model mirror and geometry -----------------------------------------------------------

void ToolbarEditStrip::onAttached() {
  setFocusable(true);
  style().flexShrink = 0.0;
  style().alignSelf = layout::Align::Start;
  contextMenu_ = std::make_unique<MenuController>(ui());
  controller_.drag().addTarget(id(), this);
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  listener_ = controller_.subscribe([context, self] {
    if (ToolbarEditStrip* s = context->objectAs<ToolbarEditStrip>(self)) s->rebuild();
  });
  rebuild();
}

void ToolbarEditStrip::onDetached() {
  cancelDrag();
  controller_.unsubscribe(listener_);
  controller_.drag().removeTarget(id());
  if (contextMenu_) contextMenu_->close();
}

void ToolbarEditStrip::rebuild() {
  const cz::ToolbarLayout* t = cz::findToolbar(controller_.model().editView().layout, toolbarId_);
  cells_.clear();
  if (t == nullptr) {
    style().width = layout::Length::px(120.0);
    style().height = layout::Length::px(40.0);
    requestLayout();
    return;
  }
  vertical_ = t->orientation == cz::Orientation::Vertical;
  locked_ = t->locked;
  step_ = cz::sizeStepPixels(t->sizeStep);
  gap_ = t->gap;
  double pos = kStripPad;
  for (const cz::Node& n : t->items) {
    Cell c;
    c.id = n.id;
    c.kind = n.kind;
    c.visible = n.visible;
    c.locked = n.locked || t->locked;
    c.user = n.user;
    c.missing = n.missing;
    switch (n.kind) {
      case cz::Kind::Command:
        c.label = controller_.model().shownLabel(n);
        c.icon = n.missing ? "circle-alert" : cust::commandIconOf(controller_.services(), n.commandId);
        c.size = step_;
        break;
      case cz::Kind::Group: {
        const cz::Node* first = n.children.empty() ? nullptr : &n.children.front();
        c.label = first != nullptr ? controller_.model().shownLabel(*first) + " (group)" : "Group";
        c.icon = first != nullptr ? cust::commandIconOf(controller_.services(), first->commandId) : "circle";
        c.size = step_ + kStripTrigger;
        break;
      }
      case cz::Kind::Separator:
        c.label = "Separator";
        c.size = kStripSeparator;
        break;
      default:
        c.label = "Spacer";
        c.size = kStripSpacer;
        break;
    }
    c.pos = pos;
    pos += c.size + gap_;
    cells_.push_back(std::move(c));
  }
  const double main = cells_.empty() ? 140.0 : pos - gap_ + kStripPad;
  const double cross = step_ + 2 * kStripPad + kStripBand;
  style().width = layout::Length::px(vertical_ ? cross : main);
  style().height = layout::Length::px(vertical_ ? main : cross);
  if (!cursor_.empty() && itemIndex(cursor_) < 0) cursor_.clear();
  indicator_ = {};
  requestLayout();
  requestPaint();
}

layout::RectD ToolbarEditStrip::cellRect(const Cell& c) const {
  const layout::Rect self = ui().absRect(id());
  const double crossLength = (c.kind == cz::Kind::Separator) ? 20.0 : step_;
  const double crossOffset = (step_ - crossLength) * 0.5;
  if (vertical_) return {self.x + kStripPad + crossOffset, self.y + c.pos, crossLength, c.size};
  return {self.x + c.pos, self.y + kStripBand + kStripPad + crossOffset, c.size, crossLength};
}

layout::RectD ToolbarEditStrip::eyeRect(const Cell& c) const {
  const layout::Rect self = ui().absRect(id());
  if (vertical_) return {self.x + kStripPad + step_ + 2.0, self.y + c.pos + c.size * 0.5 - 7.0, 14.0, 14.0};
  return {self.x + c.pos + c.size * 0.5 - 7.0, self.y + 1.0, 14.0, 14.0};
}

ToolbarEditStrip::ItemView ToolbarEditStrip::item(size_t index) const {
  ItemView v;
  if (index >= cells_.size()) return v;
  const Cell& c = cells_[index];
  v.id = c.id;
  v.kind = c.kind;
  v.label = c.label;
  v.icon = c.icon;
  v.visible = c.visible;
  v.locked = c.locked;
  v.user = c.user;
  v.missing = c.missing;
  v.cursor = c.id == cursor_;
  v.rect = cellRect(c);
  v.eye = eyeRect(c);
  return v;
}

int ToolbarEditStrip::itemIndex(const std::string& id) const {
  for (size_t i = 0; i < cells_.size(); ++i) {
    if (cells_[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

void ToolbarEditStrip::setCursor(const std::string& id) {
  if (id == cursor_) return;
  cursor_ = id;
  requestPaint();
}

ToolbarEditStrip::Hit ToolbarEditStrip::hitAt(double x, double y) const {
  Hit hit;
  for (size_t i = 0; i < cells_.size(); ++i) {
    if (cust::inside(eyeRect(cells_[i]), x, y)) {
      hit.eye = true;
      hit.index = static_cast<int>(i);
      return hit;
    }
  }
  for (size_t i = 0; i < cells_.size(); ++i) {
    layout::RectD r = cellRect(cells_[i]);
    // Cells are narrow targets: the gap on both sides belongs to them.
    if (vertical_) {
      r.y -= gap_ * 0.5;
      r.h += gap_;
    } else {
      r.x -= gap_ * 0.5;
      r.w += gap_;
    }
    if (cust::inside(r, x, y)) {
      hit.index = static_cast<int>(i);
      return hit;
    }
  }
  return hit;
}

// ---- strip: painting ----------------------------------------------------------------------------

Cursor ToolbarEditStrip::cursor() const { return hover_.index >= 0 ? (hover_.eye ? Cursor::Pointer : Cursor::Move) : Cursor::Default; }

void ToolbarEditStrip::paint(PaintContext& ctx) {
  const layout::Rect self = ctx.rect();
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(12.0));
  ctx.painter().fillRoundedRect(ctx.box(), radii, ctx.color("panel"));
  ctx.painter().border(ctx.box(), radii, ctx.hairline(), ctx.color("border"));
  const render::Color muted = ctx.color("muted");
  const render::Color accent = ctx.color("accent");
  for (size_t i = 0; i < cells_.size(); ++i) {
    const Cell& c = cells_[i];
    const layout::RectD r = cellRect(c);
    const bool hovered = hover_.index == static_cast<int>(i);
    if (!c.visible) ctx.painter().pushOpacity(0.45f);
    switch (c.kind) {
      case cz::Kind::Separator: {
        if (vertical_) {
          ctx.painter().fillRect(ctx.toPhysical(r.x, r.y + r.h * 0.5, r.w, 1.0), ctx.color("border", 0.5));
        } else {
          ctx.painter().fillRect(ctx.toPhysical(r.x + r.w * 0.5, r.y, 1.0, r.h), ctx.color("border", 0.5));
        }
        break;
      }
      case cz::Kind::Spacer: {
        ctx.painter().border(ctx.toPhysical(r.x, r.y, r.w, r.h), render::CornerRadii::uniform(ctx.px(6.0)), ctx.hairline(), ctx.color("border"));
        cust::drawIconSafe(ctx, vertical_ ? "move-vertical" : "move-horizontal", 14.0, ctx.toPhysical(r.x, r.y, r.w, r.h), muted);
        break;
      }
      default: {
        const double main = std::min(step_, c.size);
        const layout::RectD button = vertical_ ? layout::RectD{r.x, r.y, r.w, main} : layout::RectD{r.x, r.y, main, r.h};
        if (hovered) ctx.painter().fillRoundedRect(ctx.toPhysical(button.x, button.y, button.w, button.h), render::CornerRadii::uniform(ctx.px(8.0)), ctx.color("hover"));
        cust::drawIconSafe(ctx, c.icon, 16.0, ctx.toPhysical(button.x, button.y, button.w, button.h), c.missing ? ctx.color("danger") : (hovered ? ctx.color("surface") : muted));
        if (c.kind == cz::Kind::Group) {
          const layout::RectD chevron = vertical_ ? layout::RectD{r.x, r.y + main, r.w, kStripTrigger} : layout::RectD{r.x + main, r.y, kStripTrigger, r.h};
          cust::drawIconSafe(ctx, vertical_ ? "chevron-right" : "chevron-down", 12.0, ctx.toPhysical(chevron.x, chevron.y, chevron.w, chevron.h), muted);
        }
        break;
      }
    }
    if (c.id == cursor_) {
      ctx.painter().border(ctx.toPhysical(r.x - 1.0, r.y - 1.0, r.w + 2.0, r.h + 2.0), render::CornerRadii::uniform(ctx.px(8.0)), ctx.px(1.5), accent);
    }
    const layout::RectD eye = eyeRect(c);
    cust::drawIconSafe(ctx, c.locked ? "lock" : (c.visible ? "eye" : "eye-off"), 12.0, ctx.toPhysical(eye.x, eye.y, eye.w, eye.h), muted);
    if (!c.visible) ctx.painter().popOpacity();
  }
  if (cells_.empty()) {
    const theme::TextStyle s = ctx.style("label.muted").text;
    TextOptions o;
    o.align = TextAlign::Center;
    o.color = ctx.color(s.color);
    ctx.drawText("Drop a command here", s, ctx.toPhysical(self.x, self.y + kStripBand, self.w, step_ + 2 * kStripPad), o);
  }
  if (indicator_.active) {
    const layout::RectD& l = indicator_.line;
    ctx.painter().fillRoundedRect(ctx.toPhysical(l.x, l.y, l.w, l.h), render::CornerRadii::uniform(ctx.px(1.0)), accent);
  }
  if (focusVisible()) ctx.focusRing(radii.topLeft);
}

// ---- editor: header and strip -------------------------------------------------------------------

const std::vector<double>& ToolbarEditor::gapPresets() {
  static const std::vector<double> presets{0.0, 2.0, 4.0, 8.0, 12.0};
  return presets;
}

ToolbarEditStrip& ToolbarEditor::strip() const { return *ui().objectAs<ToolbarEditStrip>(strip_); }

void ToolbarEditor::onAttached() {
  style().direction = layout::FlexDirection::Column;
  style().gapRow = 6.0;
  style().flexShrink = 0.0;
  style().alignSelf = layout::Align::Start;
  cust::CustomizeBox& header = cust::row(ui(), id(), 8.0);
  header.style().flexShrink = 0.0;
  const cz::ToolbarLayout* t = cz::findToolbar(controller_.model().editView().layout, toolbarId_);
  Label& title = ui().create<Label>(header.id(), t != nullptr && !t->title.empty() ? t->title : toolbarId_, LabelRole::Heading);
  title.style().flexShrink = 0.0;
  ui().create<Label>(header.id(), "Size", LabelRole::Muted).style().flexShrink = 0.0;
  Segmented& size = ui().create<Segmented>(header.id(), SegmentedSize::Sm);
  size.setItems({{"Small", "", "Small buttons"}, {"Medium", "", "Medium buttons"}, {"Large", "", "Large buttons"}});
  size.setAccessibleName("Button size");
  size_ = size.id();
  ui().create<Label>(header.id(), "Gap", LabelRole::Muted).style().flexShrink = 0.0;
  Segmented& gap = ui().create<Segmented>(header.id(), SegmentedSize::Sm);
  std::vector<SegmentItem> gaps;
  for (const double g : gapPresets()) gaps.push_back({std::to_string(static_cast<int>(g)), "", "Gap between buttons: " + std::to_string(static_cast<int>(g)) + " px"});
  gap.setItems(std::move(gaps));
  gap.setAccessibleName("Button gap");
  gap_ = gap.id();
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  size.setOnChange([context, self](int index) {
    ToolbarEditor* e = context->objectAs<ToolbarEditor>(self);
    if (e == nullptr || index < 0 || index > 2) return;
    const cz::SizeStep steps[] = {cz::SizeStep::Small, cz::SizeStep::Medium, cz::SizeStep::Large};
    e->controller_.noteResult(e->controller_.model().setToolbarSizeStep(e->toolbarId_, steps[index]));
  });
  gap.setOnChange([context, self](int index) {
    ToolbarEditor* e = context->objectAs<ToolbarEditor>(self);
    if (e == nullptr || index < 0 || index >= static_cast<int>(gapPresets().size())) return;
    e->controller_.noteResult(e->controller_.model().setToolbarGap(e->toolbarId_, gapPresets()[static_cast<size_t>(index)]));
  });
  strip_ = ui().create<ToolbarEditStrip>(id(), controller_, toolbarId_).id();
  listener_ = controller_.subscribe([context, self] {
    if (ToolbarEditor* e = context->objectAs<ToolbarEditor>(self)) e->sync();
  });
  sync();
}

void ToolbarEditor::onDetached() { controller_.unsubscribe(listener_); }

void ToolbarEditor::sync() {
  const cz::ToolbarLayout* t = cz::findToolbar(controller_.model().editView().layout, toolbarId_);
  Segmented* size = ui().objectAs<Segmented>(size_);
  Segmented* gap = ui().objectAs<Segmented>(gap_);
  if (t == nullptr || size == nullptr || gap == nullptr) return;
  size->setSelectedIndex(static_cast<int>(t->sizeStep));
  int match = -1;
  for (size_t i = 0; i < gapPresets().size(); ++i) {
    if (gapPresets()[i] == t->gap) match = static_cast<int>(i);
  }
  gap->setSelectedIndex(match);
  size->setEnabled(!t->locked);
  gap->setEnabled(!t->locked);
}

}  // namespace r1ui::widgets
