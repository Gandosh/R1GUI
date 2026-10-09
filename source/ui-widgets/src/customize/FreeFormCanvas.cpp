// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of FreeFormPanel.h, part 2: the FreeFormCanvas display: the lifecycle, the mirror of
//   the model's edit view, the geometry queries (button rectangles, handles, hit tests) and the painting
//   of the edit display (buttons, grid, selection outlines, handles, marquee, drop preview). The
//   interaction is in FreeFormCanvasInput.cpp.
// Invariants: while a drag runs the canvas holds provisional rectangles in preview_ (already fitted by
//   Customization::fitRect); the selection only holds ids that exist (pruned in rebuild()); outside edit
//   mode there are FreeFormButton children and the canvas takes no pointer input of its own.
// Callers: FreeFormPanel, hosts, tests.
#include <algorithm>
#include <cmath>

#include "CustomizeCommon.h"
#include "FreeFormCommon.h"
#include "r1ui/widgets/customize/FreeFormPanel.h"

namespace r1ui::widgets {

namespace cz = commands::customize;
namespace layout = core::layout;
using cust::childrenOf;
using cust::paintContent;
using cust::toD;

// ---- FreeFormCanvas: model mirror ---------------------------------------------------------------

void FreeFormCanvas::onAttached() {
  setFocusable(true);
  style().flexShrink = 0.0;
  style().alignSelf = layout::Align::Start;
  style().overflow = layout::Overflow::Hidden;
  contextMenu_ = std::make_unique<MenuController>(ui());
  controller_.drag().addTarget(id(), this);
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  listener_ = controller_.subscribe([context, self] {
    if (FreeFormCanvas* c = context->objectAs<FreeFormCanvas>(self)) c->rebuild();
  });
  attachment_ = controller_.sync().attach([context, self] {
    if (FreeFormCanvas* c = context->objectAs<FreeFormCanvas>(self)) c->refreshStates();
  });
  rebuild();
}

void FreeFormCanvas::onDetached() {
  attachment_.reset();
  controller_.unsubscribe(listener_);
  controller_.drag().removeTarget(id());
  if (contextMenu_) contextMenu_->close();
}

void FreeFormCanvas::rebuild() {
  const cz::FreeFormPanelLayout* p = cz::findPanel(controller_.model().editView().layout, panelId_);
  buttons_.clear();
  if (p == nullptr) {
    width_ = height_ = 0.0;
    selected_.clear();
    style().width = layout::Length::px(120.0);
    style().height = layout::Length::px(40.0);
    requestLayout();
    rebuildChildren();
    return;
  }
  width_ = p->width;
  height_ = p->height;
  snap_ = p->snap;
  grid_ = p->grid;
  locked_ = p->locked;
  for (const cz::Node& n : p->buttons) {
    Btn b;
    b.id = n.id;
    b.commandId = n.commandId;
    b.label = controller_.model().shownLabel(n);
    b.visible = n.visible;
    b.locked = n.locked || p->locked;
    b.missing = n.missing;
    b.user = n.user;
    b.rect = {n.rect.x, n.rect.y, n.rect.w, n.rect.h};
    buttons_.push_back(std::move(b));
  }
  selected_.erase(std::remove_if(selected_.begin(), selected_.end(), [&](const std::string& s) { return buttonIndex(s) < 0; }), selected_.end());
  style().width = layout::Length::px(width_);
  style().height = layout::Length::px(height_);
  requestLayout();
  requestPaint();
  rebuildChildren();
}

void FreeFormCanvas::rebuildChildren() {
  // Children are the live command buttons of normal mode; edit mode paints the buttons itself.
  const std::vector<core::tree::WidgetId> old = childrenOf(ui(), id());
  for (const core::tree::WidgetId child : old) ui().destroy(child);
  if (editing()) return;
  for (const Btn& b : buttons_) {
    if (!b.visible || b.missing) continue;
    const cz::Node* node = controller_.model().find(b.id);
    FreeFormButton& button = ui().create<FreeFormButton>(id(), controller_, b.id, b.commandId, node != nullptr ? node->userLabel : std::string());
    button.style().inset[layout::kLeft] = layout::Length::px(b.rect.x);
    button.style().inset[layout::kTop] = layout::Length::px(b.rect.y);
    button.style().width = layout::Length::px(b.rect.w);
    button.style().height = layout::Length::px(b.rect.h);
  }
}

void FreeFormCanvas::refreshStates() {
  if (editing()) return;
  for (const core::tree::WidgetId child : childrenOf(ui(), id())) {
    if (FreeFormButton* b = ui().objectAs<FreeFormButton>(child)) b->refresh();
  }
}

FreeFormButton* FreeFormCanvas::buttonWidget(const std::string& nodeId) const {
  for (const core::tree::WidgetId child : childrenOf(ui(), id())) {
    FreeFormButton* b = ui().objectAs<FreeFormButton>(child);
    if (b != nullptr && b->nodeId() == nodeId) return b;
  }
  return nullptr;
}

// ---- geometry and queries -----------------------------------------------------------------------

layout::RectD FreeFormCanvas::toWindow(const layout::RectD& r) const {
  const layout::Rect self = ui().absRect(id());
  return {self.x + r.x, self.y + r.y, r.w, r.h};
}

layout::RectD FreeFormCanvas::current(const Btn& b) const {
  const auto it = preview_.find(b.id);
  return it != preview_.end() ? it->second : b.rect;
}

FreeFormCanvas::ButtonView FreeFormCanvas::button(size_t index) const {
  ButtonView v;
  if (index >= buttons_.size()) return v;
  const Btn& b = buttons_[index];
  v.id = b.id;
  v.commandId = b.commandId;
  v.label = b.label;
  v.visible = b.visible;
  v.selected = isSelected(b.id);
  v.locked = b.locked;
  v.missing = b.missing;
  v.user = b.user;
  v.rect = toWindow(current(b));
  return v;
}

int FreeFormCanvas::buttonIndex(const std::string& nodeId) const {
  for (size_t i = 0; i < buttons_.size(); ++i) {
    if (buttons_[i].id == nodeId) return static_cast<int>(i);
  }
  return -1;
}

bool FreeFormCanvas::isSelected(const std::string& nodeId) const { return std::find(selected_.begin(), selected_.end(), nodeId) != selected_.end(); }

void FreeFormCanvas::select(const std::string& nodeId, bool additive) {
  if (buttonIndex(nodeId) < 0) return;
  if (!additive) selected_.clear();
  if (!isSelected(nodeId)) selected_.push_back(nodeId);
  requestPaint();
}

void FreeFormCanvas::clearSelection() {
  if (selected_.empty()) return;
  selected_.clear();
  requestPaint();
}

void FreeFormCanvas::selectAll() {
  selected_.clear();
  for (const Btn& b : buttons_) selected_.push_back(b.id);
  requestPaint();
}

std::array<layout::RectD, 8> FreeFormCanvas::handles(const std::string& nodeId) const {
  std::array<layout::RectD, 8> out{};
  const int index = buttonIndex(nodeId);
  if (index < 0) return out;
  const layout::RectD r = toWindow(current(buttons_[static_cast<size_t>(index)]));
  const double xs[3] = {r.x, r.x + r.w * 0.5, r.x + r.w};
  const double ys[3] = {r.y, r.y + r.h * 0.5, r.y + r.h};
  const int order[8][2] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 1}};
  for (int i = 0; i < 8; ++i) out[static_cast<size_t>(i)] = {xs[order[i][0]] - kHandle * 0.5, ys[order[i][1]] - kHandle * 0.5, kHandle, kHandle};
  return out;
}

int FreeFormCanvas::hitButton(double lx, double ly) const {
  for (size_t i = buttons_.size(); i-- > 0;) {
    const layout::RectD r = current(buttons_[i]);
    if (cust::inside(r, lx, ly)) return static_cast<int>(i);
  }
  return -1;
}

int FreeFormCanvas::hitHandle(double lx, double ly, std::string& nodeId) const {
  if (selected_.size() != 1) return -1;
  const layout::Rect self = ui().absRect(id());
  const auto hs = handles(selected_.front());
  for (int i = 0; i < 8; ++i) {
    layout::RectD h = hs[static_cast<size_t>(i)];
    h.x -= self.x + 2.0;
    h.y -= self.y + 2.0;
    h.w += 4.0;
    h.h += 4.0;
    if (cust::inside(h, lx, ly)) {
      nodeId = selected_.front();
      return i;
    }
  }
  return -1;
}

Cursor FreeFormCanvas::cursor() const {
  if (!editing()) return Cursor::Default;
  switch (hoverHandle_) {
    case 0:
    case 4: return Cursor::ResizeNwSe;
    case 2:
    case 6: return Cursor::ResizeNeSw;
    case 1:
    case 5: return Cursor::ResizeVertical;
    case 3:
    case 7: return Cursor::ResizeHorizontal;
    default: break;
  }
  return hover_ >= 0 ? Cursor::Move : Cursor::Default;
}

// ---- painting -----------------------------------------------------------------------------------

void FreeFormCanvas::paintButton(PaintContext& ctx, const layout::RectD& rect, const Btn& b, bool hovered) {
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(8.0));
  const render::Rect box = ctx.toPhysical(rect.x, rect.y, rect.w, rect.h);
  ctx.painter().fillRoundedRect(box, radii, hovered ? ctx.color("hover") : ctx.color("hover", 0.35));
  ctx.painter().border(box, radii, ctx.hairline(), ctx.color("border"));
  const render::Color tint = b.missing ? ctx.color("danger") : ctx.color(hovered ? "surface" : "muted");
  paintContent(ctx, rect, b.missing ? "circle-alert" : cust::commandIconOf(controller_.services(), b.commandId), b.label, tint);
}

void FreeFormCanvas::paint(PaintContext& ctx) {
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(8.0));
  ctx.painter().fillRoundedRect(ctx.box(), radii, ctx.color("panel"));
  ctx.painter().border(ctx.box(), radii, ctx.hairline(), ctx.color("border"));
  if (!editing()) return;
  const layout::Rect self = ctx.rect();
  if (snap_ && grid_ >= 2.0) {
    ctx.painter().pushClip(ctx.box());
    const render::Color line = ctx.color("border", 0.25);
    for (double x = grid_; x < width_; x += grid_) ctx.painter().fillRect(ctx.toPhysical(self.x + x, self.y, 1.0, height_), line);
    for (double y = grid_; y < height_; y += grid_) ctx.painter().fillRect(ctx.toPhysical(self.x, self.y + y, width_, 1.0), line);
    ctx.painter().popClip();
  }
  ctx.painter().pushClip(ctx.box());
  for (size_t i = 0; i < buttons_.size(); ++i) {
    const Btn& b = buttons_[i];
    if (!b.visible) ctx.painter().pushOpacity(0.45f);
    paintButton(ctx, toWindow(current(b)), b, hover_ == static_cast<int>(i));
    if (!b.visible) {
      const layout::RectD r = toWindow(current(b));
      cust::drawIconSafe(ctx, "eye-off", 12.0, ctx.toPhysical(r.x + r.w - 16.0, r.y + 2.0, 14.0, 14.0), ctx.color("muted"));
      ctx.painter().popOpacity();
    }
  }
  ctx.painter().popClip();
}

void FreeFormCanvas::paintOver(PaintContext& ctx) {
  if (!editing()) return;
  const render::Color accent = ctx.color("accent");
  for (const std::string& selected : selected_) {
    const int index = buttonIndex(selected);
    if (index < 0) continue;
    const layout::RectD r = toWindow(current(buttons_[static_cast<size_t>(index)]));
    ctx.painter().border(ctx.toPhysical(r.x, r.y, r.w, r.h), render::CornerRadii::uniform(ctx.px(8.0)), ctx.px(2.0), accent);
  }
  if (selected_.size() == 1) {
    const auto hs = handles(selected_.front());
    for (const layout::RectD& h : hs) {
      const render::Rect box = ctx.toPhysical(h.x, h.y, h.w, h.h);
      ctx.painter().fillRect(box, render::Color{1.0f, 1.0f, 1.0f, 1.0f});
      ctx.painter().border(box, render::CornerRadii::uniform(0.0f), ctx.px(1.5), accent);
    }
  }
  if (marquee_) {
    const layout::RectD m = toWindow(*marquee_);
    ctx.painter().fillRect(ctx.toPhysical(m.x, m.y, m.w, m.h), ctx.color("accent", 0.12));
    ctx.painter().border(ctx.toPhysical(m.x, m.y, m.w, m.h), render::CornerRadii::uniform(0.0f), ctx.hairline(), accent);
  }
  if (dropPreview_) {
    ctx.painter().border(ctx.toPhysical(dropPreview_->x, dropPreview_->y, dropPreview_->w, dropPreview_->h), render::CornerRadii::uniform(ctx.px(8.0)), ctx.px(2.0), accent);
  }
  if (focusVisible()) ctx.focusRing(ctx.px(8.0));
}

void FreeFormCanvas::onFocusIn(Event&) { requestPaint(); }
void FreeFormCanvas::onFocusOut(Event&) { requestPaint(); }

}  // namespace r1ui::widgets
