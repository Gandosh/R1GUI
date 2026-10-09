// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of FreeFormPanel.h, part 1: FreeFormButton (the live command button of normal
//   mode) and FreeFormPanel (the header with the snap switch and the canvas). The canvas is in
//   FreeFormCanvas.cpp (display, model mirror) and FreeFormCanvasInput.cpp (interaction).
// Invariants: a FreeFormButton re-reads its command on refresh() and changes a widget only when the value
//   differs; the panel header is shown only in edit mode; all geometry changes go through Customization.
// Callers: hosts, the gallery, tests.
#include "r1ui/widgets/customize/FreeFormPanel.h"

#include "CustomizeBox.h"
#include "CustomizeCommon.h"
#include "FreeFormCommon.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/switch/Switch.h"

namespace r1ui::widgets {

namespace cz = commands::customize;
namespace layout = core::layout;
namespace State = theme::State;
using cust::paintContent;

// ---- FreeFormButton -----------------------------------------------------------------------------

void FreeFormButton::onAttached() {
  Pressable::onAttached();
  style().position = layout::Position::Absolute;
  style().flexShrink = 0.0;
  refresh();
}

void FreeFormButton::refresh() {
  const commands::CommandDef* def = controller_.services().registry.find(commandId_);
  const layout::Display wanted = def != nullptr && def->isVisible() ? layout::Display::Flex : layout::Display::None;
  if (style().display != wanted) {
    style().display = wanted;
    requestLayout();
  }
  if (def == nullptr) {
    setEnabled(false);
    return;
  }
  toggle_ = def->kind == commands::CommandKind::Toggle;
  setEnabled(def->isEnabled());
  if (def->kind == commands::CommandKind::Toggle || def->kind == commands::CommandKind::Radio) setSelected(def->isChecked());
  const std::string tip = commandTooltip(controller_.services(), *def, def->tooltip);
  if (tooltipText() != tip) setTooltip(tip);
  const std::string label = userLabel_.empty() ? def->label : userLabel_;
  const std::string icon = def->icon.empty() ? std::string("circle") : def->icon;
  if (label != label_ || icon != icon_) {
    label_ = label;
    icon_ = icon;
    requestPaint();
  }
}

void FreeFormButton::activate() {
  controller_.services().router.execute(commandId_, commands::ExecuteSource::Toolbar);
  controller_.sync().refresh();
}

uint8_t FreeFormButton::styleState() const {
  uint8_t s = Pressable::styleState();
  if (!focusVisible()) s &= static_cast<uint8_t>(~State::kFocus);
  return s;
}

float FreeFormButton::paintOpacity() const {
  return static_cast<float>(ui().services().resolve(toggle_ ? "toolbar.toggle" : "toolbar.button", styleState()).opacity);
}

void FreeFormButton::paint(PaintContext& ctx) {
  const char* key = toggle_ ? "toolbar.toggle" : "toolbar.button";
  const theme::ResolvedStyle& rs = ctx.style(key);
  const render::Color bg = ctx.animatedColor(0, ctx.color(rs.background));
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(rs.radius));
  if (bg.a > 0.0f) ctx.painter().fillRoundedRect(ctx.box(), radii, bg);
  const render::Color tint = ctx.animatedColor(1, ctx.color(rs.text.color));
  const layout::Rect r = ctx.rect();
  paintContent(ctx, {static_cast<double>(r.x), static_cast<double>(r.y), static_cast<double>(r.w), static_cast<double>(r.h)}, icon_, label_, tint);
}

void FreeFormButton::paintOver(PaintContext& ctx) {
  if (focusVisible()) ctx.focusRing(ctx.px(ctx.style(toggle_ ? "toolbar.toggle" : "toolbar.button").radius));
}

std::string_view FreeFormButton::accessibleName() const { return label_; }

// ---- the panel: header and canvas ---------------------------------------------------------------

FreeFormCanvas& FreeFormPanel::canvas() const { return *ui().objectAs<FreeFormCanvas>(canvas_); }

void FreeFormPanel::onAttached() {
  style().direction = layout::FlexDirection::Column;
  style().gapRow = 6.0;
  style().flexShrink = 0.0;
  style().alignSelf = layout::Align::Start;
  const cz::FreeFormPanelLayout* p = cz::findPanel(controller_.model().editView().layout, panelId_);
  cust::CustomizeBox& header = cust::row(ui(), id(), 8.0);
  header.style().flexShrink = 0.0;
  header_ = header.id();
  ui().create<Label>(header.id(), p != nullptr && !p->title.empty() ? p->title : panelId_, LabelRole::Heading).style().flexShrink = 0.0;
  Switch& snap = ui().create<Switch>(header.id(), SwitchSize::Sm);
  snap.setAccessibleName("Snap to grid");
  snap_ = snap.id();
  gridLabel_ = ui().create<Label>(header.id(), "Snap to grid", LabelRole::Muted).id();
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  snap.setOnChange([context, self](bool on) {
    FreeFormPanel* panel = context->objectAs<FreeFormPanel>(self);
    if (panel == nullptr) return;
    const cz::FreeFormPanelLayout* layoutNow = cz::findPanel(panel->controller_.model().editView().layout, panel->panelId_);
    panel->controller_.noteResult(panel->controller_.model().setPanelSnap(panel->panelId_, on, layoutNow != nullptr ? layoutNow->grid : cz::kDefaultGridSize));
  });
  core::tree::WidgetId add = ui().create<Button>(header.id(), "Add command...", ButtonTone::Panel, ButtonSize::Sm).id();
  ui().objectAs<Button>(add)->setOnClick([context, self] {
    if (FreeFormPanel* panel = context->objectAs<FreeFormPanel>(self)) panel->canvas().addCommandAtCursor();
  });
  canvas_ = ui().create<FreeFormCanvas>(id(), controller_, panelId_).id();
  listener_ = controller_.subscribe([context, self] {
    if (FreeFormPanel* panel = context->objectAs<FreeFormPanel>(self)) panel->sync();
  });
  sync();
}

void FreeFormPanel::onDetached() { controller_.unsubscribe(listener_); }

void FreeFormPanel::sync() {
  if (WidgetObject* h = ui().object(header_)) {
    const layout::Display wanted = controller_.editMode() ? layout::Display::Flex : layout::Display::None;
    if (h->style().display != wanted) {
      h->style().display = wanted;
      h->requestLayout();
    }
  }
  const cz::FreeFormPanelLayout* p = cz::findPanel(controller_.model().editView().layout, panelId_);
  if (p == nullptr) return;
  if (Switch* s = ui().objectAs<Switch>(snap_)) {
    s->setChecked(p->snap);
    s->setEnabled(!p->locked);
  }
  if (Label* l = ui().objectAs<Label>(gridLabel_)) l->setText("Snap to grid (" + std::to_string(static_cast<int>(p->grid)) + " px)");
}

}  // namespace r1ui::widgets
