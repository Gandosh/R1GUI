// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ToolbarEditor.h: the strip of cells, its drag and drop, the eye badges, the
//   keyboard, the context menu, and the header controls that set the size step and the gap.
// Invariants: cells_ mirror Customization::editView() as of the last rebuild() (called on every model or
//   edit-mode notification); the strip's size is always set from the cells, so layout never needs a
//   measure callback; a drag always ends through the hub; the header controls only reflect the model
//   (changing one calls the model, the notification updates the control).
// Callers: CustomizableToolbar, the gallery, tests.
#include "r1ui/widgets/customize/ToolbarEditor.h"

#include <algorithm>
#include <cmath>

#include "CustomizeBox.h"
#include "CustomizeCommon.h"
#include "r1ui/widgets/customize/CommandPicker.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/segmented/Segmented.h"

namespace r1ui::widgets {

namespace cz = commands::customize;
namespace layout = core::layout;
using core::events::Button;
using core::events::Key;
namespace Mod = core::events::Mod;

namespace {

constexpr double kPad = 5.0;        // surface padding (4 plus the border)
constexpr double kBand = 16.0;      // the eye badges' band along the strip
constexpr double kSeparator = 4.0;  // a separator cell's length
constexpr double kSpacer = 28.0;
constexpr double kTrigger = 12.0;   // the flyout chevron's width

cz::EditResult runDrop(cz::Customization& m, const std::string& toolbar, const DragPayload& payload, const cz::Placement& at) {
  (void)toolbar;
  if (payload.kind == DragPayload::Kind::Command) return m.addCommand(at.parent, payload.commandId, at.anchor, at.side);
  return m.move(payload.nodeId, at);
}

}  // namespace

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
  double pos = kPad;
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
        c.size = step_ + kTrigger;
        break;
      }
      case cz::Kind::Separator:
        c.label = "Separator";
        c.size = kSeparator;
        break;
      default:
        c.label = "Spacer";
        c.size = kSpacer;
        break;
    }
    c.pos = pos;
    pos += c.size + gap_;
    cells_.push_back(std::move(c));
  }
  const double main = cells_.empty() ? 140.0 : pos - gap_ + kPad;
  const double cross = step_ + 2 * kPad + kBand;
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
  if (vertical_) return {self.x + kPad + crossOffset, self.y + c.pos, crossLength, c.size};
  return {self.x + c.pos, self.y + kBand + kPad + crossOffset, c.size, crossLength};
}

layout::RectD ToolbarEditStrip::eyeRect(const Cell& c) const {
  const layout::Rect self = ui().absRect(id());
  if (vertical_) return {self.x + kPad + step_ + 2.0, self.y + c.pos + c.size * 0.5 - 7.0, 14.0, 14.0};
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
          const layout::RectD chevron = vertical_ ? layout::RectD{r.x, r.y + main, r.w, kTrigger} : layout::RectD{r.x + main, r.y, kTrigger, r.h};
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
    ctx.drawText("Drop a command here", s, ctx.toPhysical(self.x, self.y + kBand, self.w, step_ + 2 * kPad), o);
  }
  if (indicator_.active) {
    const layout::RectD& l = indicator_.line;
    ctx.painter().fillRoundedRect(ctx.toPhysical(l.x, l.y, l.w, l.h), render::CornerRadii::uniform(ctx.px(1.0)), accent);
  }
  if (focusVisible()) ctx.focusRing(radii.topLeft);
}

// ---- strip: pointer -----------------------------------------------------------------------------

void ToolbarEditStrip::onPointerMove(Event& e) {
  if (dragging_) {
    controller_.drag().move(e.x, e.y);
    e.markHandled();
    return;
  }
  const Hit hit = hitAt(e.x, e.y);
  std::string tip;
  if (hit.index >= 0) {
    const Cell& c = cells_[static_cast<size_t>(hit.index)];
    tip = c.locked ? controller_.model().lockReason(c.id) : (hit.eye ? (c.visible ? "Hide" : "Show") : c.label);
    if (c.locked && tip.empty()) tip = controller_.model().lockReason(toolbarId_);
  }
  if (hit.index != hover_.index || hit.eye != hover_.eye || tip != tip_) {
    hover_ = hit;
    tip_ = tip;
    requestPaint();
  }
}

void ToolbarEditStrip::onPointerLeave(Event&) {
  if (dragging_) return;
  hover_ = {};
  tip_.clear();
  requestPaint();
}

void ToolbarEditStrip::onPointerDown(Event& e) {
  const Hit hit = hitAt(e.x, e.y);
  if (e.button == Button::Right) {
    if (hit.index >= 0) {
      e.markHandled();
      cursor_ = cells_[static_cast<size_t>(hit.index)].id;
      openContextMenu(cursor_, e.x, e.y);
    }
    return;
  }
  if (e.button != Button::Left) return;
  ui().router().focus(id(), core::events::FocusReason::Pointer);
  e.markHandled();
  pressed_ = hit;
  armedId_.clear();
  if (hit.index < 0) return;
  cursor_ = cells_[static_cast<size_t>(hit.index)].id;
  requestPaint();
  if (!hit.eye) {
    armedId_ = cursor_;
    ui().router().capturePointer(id());
  }
}

void ToolbarEditStrip::onDragStart(Event& e) {
  if (armedId_.empty() || dragging_) return;
  DragPayload payload;
  payload.kind = DragPayload::Kind::Node;
  payload.nodeId = armedId_;
  const int index = itemIndex(armedId_);
  payload.text = index >= 0 ? cells_[static_cast<size_t>(index)].label : armedId_;
  dragging_ = controller_.drag().begin(std::move(payload), e.x, e.y);
  e.markHandled();
}

void ToolbarEditStrip::onPointerUp(Event& e) {
  if (e.button != Button::Left) return;
  const bool wasDragging = dragging_;
  dragging_ = false;
  armedId_.clear();
  if (wasDragging) controller_.drag().end(e.x, e.y);
}

void ToolbarEditStrip::onCaptureLost(Event&) { cancelDrag(); }

void ToolbarEditStrip::cancelDrag() {
  if (dragging_) controller_.drag().cancel();
  dragging_ = false;
  armedId_.clear();
}

void ToolbarEditStrip::onClick(Event& e) {
  if (e.button != Button::Left) return;
  const Hit hit = hitAt(e.x, e.y);
  if (hit.index < 0 || !hit.eye || !pressed_.eye || hit.index != pressed_.index) return;
  e.markHandled();
  toggleHidden(cells_[static_cast<size_t>(hit.index)].id);
}

void ToolbarEditStrip::onFocusIn(Event&) { requestPaint(); }
void ToolbarEditStrip::onFocusOut(Event&) { requestPaint(); }

// ---- strip: actions -----------------------------------------------------------------------------

void ToolbarEditStrip::toggleHidden(const std::string& nodeId) {
  const cz::Node* node = controller_.model().find(nodeId);
  if (node == nullptr) return;
  controller_.noteResult(controller_.model().setHidden(nodeId, node->visible));
}

void ToolbarEditStrip::removeOrHide(const std::string& nodeId) {
  const cz::Node* node = controller_.model().find(nodeId);
  if (node == nullptr) return;
  controller_.noteResult(node->user ? controller_.model().removeUserEntry(nodeId) : controller_.model().setHidden(nodeId, true));
}

void ToolbarEditStrip::moveByKey(const std::string& nodeId, int direction) {
  const int index = itemIndex(nodeId);
  if (index < 0) return;
  const int other = index + direction;
  if (other < 0 || other >= static_cast<int>(cells_.size())) return;
  const cz::Placement to{toolbarId_, cells_[static_cast<size_t>(other)].id, direction < 0 ? cz::Side::Before : cz::Side::After};
  const cz::EditResult r = controller_.model().move(nodeId, to);
  controller_.noteResult(r);
  if (r.ok) cursor_ = nodeId;
}

void ToolbarEditStrip::onKeyDown(Event& e) {
  if (e.key == Key::Escape && dragging_) {
    cancelDrag();
    ui().router().cancelPointerInteraction();
    e.markHandled();
    return;
  }
  if (e.modifiers & (Mod::kCtrl | Mod::kMeta)) return;
  const bool alt = (e.modifiers & Mod::kAlt) != 0;
  const Key previous = vertical_ ? Key::Up : Key::Left;
  const Key next = vertical_ ? Key::Down : Key::Right;
  const int index = itemIndex(cursor_);
  const auto moveCursor = [&](int to) {
    if (cells_.empty()) return;
    to = std::clamp(to, 0, static_cast<int>(cells_.size()) - 1);
    cursor_ = cells_[static_cast<size_t>(to)].id;
    requestPaint();
  };
  if (e.key == previous) {
    if (alt) {
      moveByKey(cursor_, -1);
    } else {
      moveCursor(index < 0 ? static_cast<int>(cells_.size()) - 1 : index - 1);
    }
  } else if (e.key == next) {
    if (alt) {
      moveByKey(cursor_, 1);
    } else {
      moveCursor(index < 0 ? 0 : index + 1);
    }
  } else if (e.key == Key::Home) {
    moveCursor(0);
  } else if (e.key == Key::End) {
    moveCursor(static_cast<int>(cells_.size()) - 1);
  } else if (e.key == Key::Space) {
    if (!cursor_.empty()) toggleHidden(cursor_);
  } else if (e.key == Key::Delete) {
    if (!cursor_.empty()) removeOrHide(cursor_);
  } else if (e.key == Key::Insert) {
    addCommandAtCursor();
  } else {
    return;
  }
  e.markHandled();
}

void ToolbarEditStrip::addCommandAtCursor() {
  CommandPickerOptions options;
  options.title = "Add a command to the toolbar";
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  options.onChosen = [context, self](const std::string& commandId) {
    ToolbarEditStrip* s = context->objectAs<ToolbarEditStrip>(self);
    if (s == nullptr) return;
    const bool atItem = s->itemIndex(s->cursor_) >= 0;
    s->controller_.noteResult(s->controller_.model().addCommand(s->toolbarId_, commandId, atItem ? s->cursor_ : std::string(), atItem ? cz::Side::After : cz::Side::End));
  };
  openCommandPicker(controller_, std::move(options));
}

bool ToolbarEditStrip::openContextMenu(const std::string& nodeId, double x, double y) {
  const cz::Node* node = controller_.model().find(nodeId);
  if (node == nullptr) return false;
  const bool locked = node->locked || locked_;
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  MenuSpec spec;
  const auto add = [&](const char* tag, const std::string& label, bool enabled, std::function<void(ToolbarEditStrip&)> fn) {
    MenuItemSpec item = menuAction(std::string("customize:") + tag, label);
    item.enabled = enabled;
    item.onActivate = [context, self, fn = std::move(fn)](const MenuItemSpec&) {
      if (ToolbarEditStrip* s = context->objectAs<ToolbarEditStrip>(self)) fn(*s);
    };
    spec.items.push_back(std::move(item));
  };
  add("visibility", node->visible ? "Hide" : "Show", !locked, [nodeId](ToolbarEditStrip& s) { s.toggleHidden(nodeId); });
  spec.items.push_back(menuSeparator());
  add("addCommand", "Add command...", !locked_, [nodeId](ToolbarEditStrip& s) {
    s.cursor_ = nodeId;
    s.addCommandAtCursor();
  });
  add("addSeparator", "Add separator", !locked_, [nodeId](ToolbarEditStrip& s) {
    s.controller_.noteResult(s.controller_.model().addSeparator(s.toolbarId_, nodeId, cz::Side::After));
  });
  add("addSpacer", "Add spacer", !locked_, [nodeId](ToolbarEditStrip& s) {
    s.controller_.noteResult(s.controller_.model().addSpacer(s.toolbarId_, nodeId, cz::Side::After));
  });
  if (node->user) add("remove", "Remove", !locked, [nodeId](ToolbarEditStrip& s) { s.removeOrHide(nodeId); });
  spec.items.push_back(menuSeparator());
  add("reset", "Reset this toolbar", !locked_, [](ToolbarEditStrip& s) { s.controller_.noteResult(s.controller_.model().resetMenu(s.toolbarId_)); });
  spec.onCommand = [](const MenuItemSpec&) {};
  return contextMenu_->openContextMenu(std::move(spec), x, y);
}

// ---- strip: drag and drop -----------------------------------------------------------------------

ToolbarEditStrip::Candidate ToolbarEditStrip::locate(const DragPayload& payload, double x, double y) const {
  (void)payload;
  Candidate c;
  const layout::Rect self = ui().absRect(id());
  if (x < self.x || y < self.y || x >= self.x + self.w || y >= self.y + self.h) return c;
  c.valid = true;
  const double m = vertical_ ? y - self.y : x - self.x;
  const double half = std::max(1.0, gap_ * 0.5);
  const auto lineAt = [&](double at) -> layout::RectD {
    if (vertical_) return {self.x + kPad, self.y + at - 1.0, step_, 2.0};
    return {self.x + at - 1.0, self.y + kBand + kPad, 2.0, step_};
  };
  if (cells_.empty()) {
    c.placement = {toolbarId_, "", cz::Side::End};
    c.line = lineAt(kPad);
    return c;
  }
  for (size_t i = 0; i < cells_.size(); ++i) {
    const Cell& cell = cells_[i];
    const double end = i + 1 < cells_.size() ? cells_[i + 1].pos - half : cell.pos + cell.size + kPad;
    const double start = i == 0 ? 0.0 : cell.pos - half;
    if (m < start || m >= end) continue;
    const bool before = m < cell.pos + cell.size * 0.5;
    c.placement = {toolbarId_, cell.id, before ? cz::Side::Before : cz::Side::After};
    c.line = lineAt(before ? cell.pos - half : cell.pos + cell.size + half);
    return c;
  }
  const Cell& last = cells_.back();
  c.placement = {toolbarId_, last.id, cz::Side::After};
  c.line = lineAt(last.pos + last.size + half);
  return c;
}

bool ToolbarEditStrip::dragOver(const DragPayload& payload, double x, double y) {
  indicator_ = {};
  const Candidate c = locate(payload, x, y);
  if (!c.valid) {
    requestPaint();
    return false;
  }
  const cz::EditResult r = controller_.model().preview([&](cz::Customization& m) { return runDrop(m, toolbarId_, payload, c.placement); });
  if (!r.ok) {
    if (r.error == cz::EditError::Locked) controller_.noteResult(r);
    requestPaint();
    return false;
  }
  indicator_.active = true;
  indicator_.line = c.line;
  indicator_.placement = c.placement;
  requestPaint();
  return true;
}

void ToolbarEditStrip::dragLeave() {
  if (!indicator_.active) return;
  indicator_ = {};
  requestPaint();
}

bool ToolbarEditStrip::dragDrop(const DragPayload& payload, double x, double y) {
  indicator_ = {};
  const Candidate c = locate(payload, x, y);
  if (!c.valid) return false;
  const cz::EditResult r = runDrop(controller_.model(), toolbarId_, payload, c.placement);
  controller_.noteResult(r);
  return r.ok;
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
  title_ = title.id();
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
