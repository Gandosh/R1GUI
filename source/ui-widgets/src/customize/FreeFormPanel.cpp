// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of FreeFormPanel.h: the normal-mode command buttons, the edit mode (selection,
//   move, resize, marquee, nudge, delete, align, palette drops, context menu) and the panel header.
// Invariants: geometry is changed only through Customization; while a drag runs the canvas holds
//   provisional rectangles in preview_ (already fitted by Customization::fitRect), commits them on
//   release and clears them first so a rebuild triggered by the commit never sees stale previews; the
//   selection only holds ids that exist (pruned in rebuild()); outside edit mode there are
//   FreeFormButton children and the canvas takes no pointer input of its own.
// Callers: hosts, the gallery, tests.
#include "r1ui/widgets/customize/FreeFormPanel.h"

#include <algorithm>
#include <cmath>

#include "CustomizeBox.h"
#include "CustomizeCommon.h"
#include "r1ui/widgets/button/Button.h"
#include "r1ui/widgets/customize/CommandPicker.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/switch/Switch.h"

namespace r1ui::widgets {

namespace cz = commands::customize;
namespace layout = core::layout;
namespace State = theme::State;
using MouseButton = core::events::Button;
using core::events::Key;
namespace Mod = core::events::Mod;

namespace {

constexpr double kDragThreshold = 5.0;

std::vector<core::tree::WidgetId> childrenOf(const UiContext& ui, core::tree::WidgetId parent) {
  std::vector<core::tree::WidgetId> out;
  for (core::tree::WidgetId c = ui.tree().firstChild(parent); c.valid(); c = ui.tree().nextSibling(c)) out.push_back(c);
  return out;
}

layout::RectD toD(const cz::Rect& r) { return {r.x, r.y, r.w, r.h}; }
cz::Rect toC(const layout::RectD& r) { return {r.x, r.y, r.w, r.h}; }
bool same(const layout::RectD& a, const layout::RectD& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

layout::RectD normalized(double x0, double y0, double x1, double y1) {
  return {std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0), std::abs(y1 - y0)};
}

bool intersects(const layout::RectD& a, const layout::RectD& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// Draws a free-form button's content: the icon, then the label when the button is wide enough.
void paintContent(PaintContext& ctx, const layout::RectD& r, const std::string& icon, const std::string& label, const render::Color& tint) {
  const bool showLabel = !label.empty() && r.w >= 64.0;
  const double iconBox = showLabel ? 28.0 : r.w;
  cust::drawIconSafe(ctx, icon, 16.0, ctx.toPhysical(r.x, r.y, iconBox, r.h), tint);
  if (showLabel) {
    const theme::TextStyle text = ctx.style("label.body").text;
    TextOptions o;
    o.padLeft = 28.0;
    o.padRight = 6.0;
    o.color = tint;
    ctx.drawText(label, text, ctx.toPhysical(r.x, r.y, r.w, r.h), o);
  }
}

}  // namespace

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

// ---- pointer ------------------------------------------------------------------------------------

void FreeFormCanvas::onPointerMove(Event& e) {
  if (!editing()) return;
  const layout::Rect self = ui().absRect(id());
  const double lx = e.x - self.x, ly = e.y - self.y;
  lastX_ = lx;
  lastY_ = ly;
  if (mode_ != Mode::None) {
    updateDrag(lx, ly);
    e.markHandled();
    return;
  }
  std::string handleOwner;
  const int handle = hitHandle(lx, ly, handleOwner);
  const int hit = hitButton(lx, ly);
  std::string tip;
  if (hit >= 0) {
    const Btn& b = buttons_[static_cast<size_t>(hit)];
    tip = b.locked ? controller_.model().lockReason(b.id) : b.label;
  } else if (locked_) {
    tip = controller_.model().lockReason(panelId_);
  }
  if (handle != hoverHandle_ || hit != hover_ || tip != tip_) {
    hoverHandle_ = handle;
    hover_ = hit;
    tip_ = tip;
    requestPaint();
  }
}

void FreeFormCanvas::onPointerLeave(Event&) {
  if (mode_ != Mode::None) return;
  hover_ = -1;
  hoverHandle_ = -1;
  tip_.clear();
  requestPaint();
}

void FreeFormCanvas::onPointerDown(Event& e) {
  if (!editing()) return;
  const layout::Rect self = ui().absRect(id());
  const double lx = e.x - self.x, ly = e.y - self.y;
  lastX_ = lx;
  lastY_ = ly;
  ui().router().focus(id(), core::events::FocusReason::Pointer);
  const bool additive = (e.modifiers & (Mod::kCtrl | Mod::kShift)) != 0;
  if (e.button == MouseButton::Right) {
    const int hit = hitButton(lx, ly);
    if (hit >= 0 && !isSelected(buttons_[static_cast<size_t>(hit)].id)) select(buttons_[static_cast<size_t>(hit)].id);
    e.markHandled();
    openContextMenu(e.x, e.y);
    return;
  }
  if (e.button != MouseButton::Left) return;
  e.markHandled();
  std::string handleOwner;
  const int handle = hitHandle(lx, ly, handleOwner);
  if (handle >= 0) {
    const int index = buttonIndex(handleOwner);
    if (index >= 0 && buttons_[static_cast<size_t>(index)].locked) {
      cz::EditResult refused;
      refused.error = cz::EditError::Locked;
      refused.reason = controller_.model().lockReason(handleOwner);
      controller_.noteResult(refused);
      return;
    }
    handle_ = handle;
    beginDrag(Mode::Resize, lx, ly);
    return;
  }
  const int hit = hitButton(lx, ly);
  additive_ = additive;
  collapseOnRelease_ = false;
  pressedId_.clear();
  if (hit >= 0) {
    const Btn& b = buttons_[static_cast<size_t>(hit)];
    pressedId_ = b.id;
    if (additive) {
      if (isSelected(b.id)) {
        selected_.erase(std::find(selected_.begin(), selected_.end(), b.id));
        requestPaint();
        return;
      }
      selected_.push_back(b.id);
    } else if (!isSelected(b.id)) {
      selected_ = {b.id};
    } else {
      collapseOnRelease_ = selected_.size() > 1;
    }
    if (b.locked) {
      cz::EditResult refused;
      refused.error = cz::EditError::Locked;
      refused.reason = controller_.model().lockReason(b.id);
      controller_.noteResult(refused);
      requestPaint();
      return;
    }
    beginDrag(Mode::Move, lx, ly);
    return;
  }
  marqueeBase_ = additive ? selected_ : std::vector<std::string>();
  if (!additive) selected_.clear();
  beginDrag(Mode::Marquee, lx, ly);
}

void FreeFormCanvas::beginDrag(Mode mode, double lx, double ly) {
  mode_ = mode;
  startX_ = lx;
  startY_ = ly;
  moved_ = false;
  original_.clear();
  preview_.clear();
  marquee_.reset();
  if (mode == Mode::Resize) {
    const int index = buttonIndex(selected_.front());
    if (index >= 0) original_[selected_.front()] = buttons_[static_cast<size_t>(index)].rect;
  } else if (mode == Mode::Move) {
    for (const std::string& s : selected_) {
      const int index = buttonIndex(s);
      if (index >= 0 && !buttons_[static_cast<size_t>(index)].locked) original_[s] = buttons_[static_cast<size_t>(index)].rect;
    }
  }
  ui().router().capturePointer(id());
  requestPaint();
}

void FreeFormCanvas::updateDrag(double lx, double ly) {
  const double dx = lx - startX_, dy = ly - startY_;
  if (!moved_ && std::hypot(dx, dy) <= kDragThreshold) return;
  moved_ = true;
  if (mode_ == Mode::Marquee) {
    const layout::RectD m = normalized(std::clamp(startX_, 0.0, width_), std::clamp(startY_, 0.0, height_), std::clamp(lx, 0.0, width_), std::clamp(ly, 0.0, height_));
    marquee_ = m;
    selected_ = marqueeBase_;
    for (const Btn& b : buttons_) {
      if (intersects(m, b.rect) && !isSelected(b.id)) selected_.push_back(b.id);
    }
    requestPaint();
    return;
  }
  if (original_.empty()) return;
  preview_.clear();
  if (mode_ == Mode::Move) {
    double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
    for (const auto& [id, r] : original_) {
      minX = std::min(minX, r.x);
      minY = std::min(minY, r.y);
      maxX = std::max(maxX, r.x + r.w);
      maxY = std::max(maxY, r.y + r.h);
    }
    const double cx = std::clamp(dx, -minX, std::max(-minX, width_ - maxX));
    const double cy = std::clamp(dy, -minY, std::max(-minY, height_ - maxY));
    for (const auto& [id, r] : original_) preview_[id] = toD(controller_.model().fitRect(panelId_, {r.x + cx, r.y + cy, r.w, r.h}));
  } else {
    const auto& [id, o] = *original_.begin();
    layout::RectD r = o;
    const int h = handle_;
    const bool left = h == 0 || h == 6 || h == 7, right = h == 2 || h == 3 || h == 4, top = h == 0 || h == 1 || h == 2, bottom = h == 4 || h == 5 || h == 6;
    if (left) {
      r.x = o.x + dx;
      r.w = o.w - dx;
    }
    if (right) r.w = o.w + dx;
    if (top) {
      r.y = o.y + dy;
      r.h = o.h - dy;
    }
    if (bottom) r.h = o.h + dy;
    if (r.w < cz::kMinButtonSize) {
      if (left) r.x = o.x + o.w - cz::kMinButtonSize;
      r.w = cz::kMinButtonSize;
    }
    if (r.h < cz::kMinButtonSize) {
      if (top) r.y = o.y + o.h - cz::kMinButtonSize;
      r.h = cz::kMinButtonSize;
    }
    preview_[id] = toD(controller_.model().fitRect(panelId_, toC(r)));
  }
  requestPaint();
}

void FreeFormCanvas::commitDrag() {
  auto previews = std::move(preview_);
  auto originals = std::move(original_);
  preview_.clear();
  original_.clear();
  mode_ = Mode::None;
  for (const auto& [nodeId, rect] : previews) {
    const auto it = originals.find(nodeId);
    if (it != originals.end() && same(it->second, rect)) continue;
    controller_.noteResult(controller_.model().setButtonRect(nodeId, toC(rect)));
  }
}

void FreeFormCanvas::abortDrag() {
  preview_.clear();
  original_.clear();
  marquee_.reset();
  mode_ = Mode::None;
  moved_ = false;
  requestPaint();
}

void FreeFormCanvas::onPointerUp(Event& e) {
  if (e.button != MouseButton::Left || mode_ == Mode::None) return;
  const Mode mode = mode_;
  const bool moved = moved_;
  marquee_.reset();
  if (mode == Mode::Marquee) {
    mode_ = Mode::None;
    requestPaint();
    return;
  }
  if (moved) {
    commitDrag();
  } else {
    abortDrag();
    if (mode == Mode::Move && collapseOnRelease_ && !pressedId_.empty()) selected_ = {pressedId_};
  }
  moved_ = false;
  collapseOnRelease_ = false;
  requestPaint();
}

void FreeFormCanvas::onCaptureLost(Event&) {
  if (mode_ != Mode::None) abortDrag();
}

// ---- keyboard and actions -----------------------------------------------------------------------

void FreeFormCanvas::nudge(int dx, int dy, bool large) {
  double step = 1.0;
  if (snap_) {
    step = large ? std::ceil(10.0 / grid_) * grid_ : grid_;
  } else if (large) {
    step = 10.0;
  }
  const std::vector<std::string> ids = selected_;
  for (const std::string& s : ids) {
    const int index = buttonIndex(s);
    if (index < 0) continue;
    cz::Rect r{buttons_[static_cast<size_t>(index)].rect.x, buttons_[static_cast<size_t>(index)].rect.y, buttons_[static_cast<size_t>(index)].rect.w,
               buttons_[static_cast<size_t>(index)].rect.h};
    r.x += dx * step;
    r.y += dy * step;
    controller_.noteResult(controller_.model().setButtonRect(s, r));
  }
}

void FreeFormCanvas::deleteSelection() {
  const std::vector<std::string> ids = selected_;
  selected_.clear();
  for (const std::string& s : ids) controller_.noteResult(controller_.model().deleteButton(s));
  requestPaint();
}

void FreeFormCanvas::alignSelection(char how) {
  if (selected_.size() < 2) return;
  double l = 1e18, t = 1e18, r = -1e18, b = -1e18;
  for (const std::string& s : selected_) {
    const int index = buttonIndex(s);
    if (index < 0) continue;
    const layout::RectD& rect = buttons_[static_cast<size_t>(index)].rect;
    l = std::min(l, rect.x);
    t = std::min(t, rect.y);
    r = std::max(r, rect.x + rect.w);
    b = std::max(b, rect.y + rect.h);
  }
  const std::vector<std::string> ids = selected_;
  for (const std::string& s : ids) {
    const int index = buttonIndex(s);
    if (index < 0) continue;
    layout::RectD rect = buttons_[static_cast<size_t>(index)].rect;
    switch (how) {
      case 'l': rect.x = l; break;
      case 'r': rect.x = r - rect.w; break;
      case 't': rect.y = t; break;
      case 'b': rect.y = b - rect.h; break;
      case 'h': rect.x = l + (r - l - rect.w) * 0.5; break;
      case 'v': rect.y = t + (b - t - rect.h) * 0.5; break;
      default: return;
    }
    controller_.noteResult(controller_.model().setButtonRect(s, {rect.x, rect.y, rect.w, rect.h}));
  }
}

void FreeFormCanvas::addCommandAtCursor() {
  CommandPickerOptions options;
  options.title = "Add a command to the panel";
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  options.onChosen = [context, self](const std::string& commandId) {
    FreeFormCanvas* c = context->objectAs<FreeFormCanvas>(self);
    if (c == nullptr) return;
    const cz::EditResult r = c->controller_.model().placeButton(
        c->panelId_, commandId, {c->lastX_ - kDefaultWidth * 0.5, c->lastY_ - kDefaultHeight * 0.5, kDefaultWidth, kDefaultHeight});
    c->controller_.noteResult(r);
    if (r.ok) {
      c->select(r.id);
      c->lastX_ += 12.0;
      c->lastY_ += 12.0;
    }
  };
  openCommandPicker(controller_, std::move(options));
}

void FreeFormCanvas::onKeyDown(Event& e) {
  if (!editing()) return;
  if (e.key == Key::Escape) {
    if (mode_ != Mode::None) {
      abortDrag();
      ui().router().cancelPointerInteraction();
    } else {
      clearSelection();
    }
    e.markHandled();
    return;
  }
  const bool shift = (e.modifiers & Mod::kShift) != 0;
  if (e.modifiers & Mod::kCtrl) {
    if (e.key == Key::A) {
      selectAll();
      e.markHandled();
    }
    return;
  }
  if (e.modifiers & (Mod::kAlt | Mod::kMeta)) return;
  switch (e.key) {
    case Key::Left: nudge(-1, 0, shift); break;
    case Key::Right: nudge(1, 0, shift); break;
    case Key::Up: nudge(0, -1, shift); break;
    case Key::Down: nudge(0, 1, shift); break;
    case Key::Delete:
    case Key::Backspace: deleteSelection(); break;
    case Key::Insert: addCommandAtCursor(); break;
    default: return;
  }
  e.markHandled();
}

bool FreeFormCanvas::openContextMenu(double x, double y) {
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  MenuSpec spec;
  const bool any = !selected_.empty();
  const bool many = selected_.size() >= 2;
  const auto add = [&](const char* tag, const std::string& label, bool enabled, std::function<void(FreeFormCanvas&)> fn) {
    MenuItemSpec item = menuAction(std::string("customize:") + tag, label);
    item.enabled = enabled && !locked_;
    item.onActivate = [context, self, fn = std::move(fn)](const MenuItemSpec&) {
      if (FreeFormCanvas* c = context->objectAs<FreeFormCanvas>(self)) fn(*c);
    };
    spec.items.push_back(std::move(item));
  };
  add("delete", "Delete", any, [](FreeFormCanvas& c) { c.deleteSelection(); });
  add("front", "Bring to front", any, [](FreeFormCanvas& c) {
    for (const std::string& s : std::vector<std::string>(c.selected_)) c.controller_.noteResult(c.controller_.model().bringToFront(s));
  });
  add("back", "Send to back", any, [](FreeFormCanvas& c) {
    for (const std::string& s : std::vector<std::string>(c.selected_)) c.controller_.noteResult(c.controller_.model().sendToBack(s));
  });
  spec.items.push_back(menuSeparator());
  const struct {
    const char* tag;
    const char* label;
    char how;
  } aligns[] = {{"al", "Align left", 'l'}, {"ar", "Align right", 'r'}, {"at", "Align top", 't'}, {"ab", "Align bottom", 'b'}, {"ah", "Centre horizontally", 'h'}, {"av", "Centre vertically", 'v'}};
  for (const auto& a : aligns) add(a.tag, a.label, many, [how = a.how](FreeFormCanvas& c) { c.alignSelection(how); });
  spec.items.push_back(menuSeparator());
  add("add", "Add command...", true, [](FreeFormCanvas& c) { c.addCommandAtCursor(); });
  spec.onCommand = [](const MenuItemSpec&) {};
  return contextMenu_->openContextMenu(std::move(spec), x, y);
}

// ---- palette drops ------------------------------------------------------------------------------

bool FreeFormCanvas::dragOver(const DragPayload& payload, double x, double y) {
  dropPreview_.reset();
  if (!editing() || payload.kind != DragPayload::Kind::Command) {
    requestPaint();
    return false;
  }
  const layout::Rect self = ui().absRect(id());
  const cz::Rect want{x - self.x - kDefaultWidth * 0.5, y - self.y - kDefaultHeight * 0.5, kDefaultWidth, kDefaultHeight};
  const cz::Rect fitted = controller_.model().fitRect(panelId_, want);
  const cz::EditResult r = controller_.model().preview([&](cz::Customization& m) { return m.placeButton(panelId_, payload.commandId, want); });
  if (!r.ok) {
    if (r.error == cz::EditError::Locked) controller_.noteResult(r);
    requestPaint();
    return false;
  }
  dropPreview_ = toWindow({fitted.x, fitted.y, fitted.w, fitted.h});
  requestPaint();
  return true;
}

void FreeFormCanvas::dragLeave() {
  if (!dropPreview_) return;
  dropPreview_.reset();
  requestPaint();
}

bool FreeFormCanvas::dragDrop(const DragPayload& payload, double x, double y) {
  dropPreview_.reset();
  if (!editing() || payload.kind != DragPayload::Kind::Command) return false;
  const layout::Rect self = ui().absRect(id());
  const cz::EditResult r = controller_.model().placeButton(panelId_, payload.commandId,
                                                          {x - self.x - kDefaultWidth * 0.5, y - self.y - kDefaultHeight * 0.5, kDefaultWidth, kDefaultHeight});
  controller_.noteResult(r);
  if (r.ok) select(r.id);
  return r.ok;
}

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
