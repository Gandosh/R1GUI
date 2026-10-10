// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PiePreviewEditor.h.
// Invariants: selected_/dropSlot_ are -1 or inside the draft's slot count (clamped in refresh); the hub is
//   touched only between a press on a filled slot and the release, Escape or capture loss; the drawing
//   child is the only child and is replaced, never edited; no callback runs while the child is being
//   rebuilt.
// Callers: CreateCustomMenuWindow, the gallery, tests.
#include "r1ui/widgets/custommenu/creator/PiePreviewEditor.h"

#include <cmath>

#include "r1ui/widgets/pie/PieMenu.h"
#include "r1ui/widgets/pie/PieTrigger.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/UiContext.h"

namespace r1ui::widgets {

namespace cm = commands::custommenu;
namespace layout = core::layout;
using core::events::Button;
using core::events::Key;

namespace {

bool parseEntryDrag(const std::string& nodeId, int& index) {
  const std::string prefix = kEntryDragPrefix;
  if (nodeId.size() <= prefix.size() || nodeId.compare(0, prefix.size(), prefix) != 0) return false;
  int value = 0;
  for (size_t i = prefix.size(); i < nodeId.size(); ++i) {
    if (nodeId[i] < '0' || nodeId[i] > '9' || value > 100000) return false;
    value = value * 10 + (nodeId[i] - '0');
  }
  index = value;
  return true;
}

}  // namespace

PiePreviewEditor::PiePreviewEditor(CommandServices services, CreatorSession& session) : services_(services), session_(session) {}

void PiePreviewEditor::onAttached() {
  setFocusable(true);
  style().direction = layout::FlexDirection::Column;
  style().width = layout::Length::px(PieMenu::extent());
  style().height = layout::Length::px(PieMenu::extent());
  style().flexShrink = 0.0;
  menu_ = std::make_unique<MenuController>(ui());
  refresh();
}

void PiePreviewEditor::onDetached() {
  if (dragging_ && hub_ != nullptr) hub_->cancel();
  dragging_ = armed_ = false;
  if (hub_ != nullptr) hub_->removeTarget(id());
  hub_ = nullptr;
  if (menu_) menu_->close();
}

void PiePreviewEditor::setDragHub(DragHub* hub) {
  if (hub_ != nullptr && hub_ != hub) hub_->removeTarget(id());
  hub_ = hub;
  if (hub_ != nullptr) hub_->addTarget(id(), static_cast<DragTarget*>(this));
}

// ---- drawing ----------------------------------------------------------------------------------------

void PiePreviewEditor::refresh() {
  if (ui().alive(pie_)) ui().destroy(pie_);
  pie_ = {};
  const cm::MenuDraft* d = draft();
  if (d == nullptr || d->kind() != cm::MenuKind::Pie) {
    selected_ = dropSlot_ = -1;
    requestPaint();
    return;
  }
  const cm::CustomMenu& menu = d->menu();
  std::vector<PieSlotView> views = pieSlotViews(services_, menu);
  // The preview shows every placed action as usable: a command that is disabled right now (Undo with
  // nothing to undo) is still a good choice for a slot. Only a missing command stays dim.
  for (PieSlotView& v : views) v.selectable = v.filled && !v.missing;
  PieMenu& pie = ui().create<PieMenu>(id(), std::move(views));
  pie_ = pie.id();
  if (selected_ >= menu.slotCount) selected_ = -1;
  if (dropSlot_ >= menu.slotCount) dropSlot_ = -1;
  requestPaint();
}

bool PiePreviewEditor::slotCenter(int slot, double& x, double& y) const {
  const cm::MenuDraft* d = draft();
  if (d == nullptr || slot < 0 || slot >= d->menu().slotCount) return false;
  const layout::Rect self = ui().absRect(id());
  const PiePoint p = pieSlotOffset(d->menu().slotCount, slot);
  x = self.x + self.w * 0.5 + p.x;
  y = self.y + self.h * 0.5 + p.y;
  return true;
}

int PiePreviewEditor::slotAt(double x, double y) const {
  const cm::MenuDraft* d = draft();
  if (d == nullptr || d->kind() != cm::MenuKind::Pie) return -1;
  const layout::Rect self = ui().absRect(id());
  const double dx = x - (self.x + self.w * 0.5);
  const double dy = y - (self.y + self.h * 0.5);
  if (!std::isfinite(dx) || !std::isfinite(dy) || std::hypot(dx, dy) > PieMenu::kBackdropRadius + 16.0) return -1;
  const std::optional<int> slot = pieSlotForDirection(dx, dy, d->menu().slotCount, PieGestureConfig{}.deadZone);
  return slot ? *slot : -1;
}

void PiePreviewEditor::paintOver(PaintContext& ctx) {
  const cm::MenuDraft* d = draft();
  if (d == nullptr || d->kind() != cm::MenuKind::Pie) return;
  const layout::Rect self = ctx.rect();
  const double cx = self.x + self.w * 0.5;
  const double cy = self.y + self.h * 0.5;
  const cm::CustomMenu& menu = d->menu();
  const theme::TextStyle muted = ctx.style("label.muted").text;
  render::Painter& painter = ctx.painter();
  const render::CornerRadii radii = render::CornerRadii::uniform(ctx.px(15.0));
  for (int i = 0; i < menu.slotCount; ++i) {
    const PiePoint p = pieSlotOffset(menu.slotCount, i);
    const render::Rect box = ctx.toPhysical(cx + p.x - PieMenu::kSlotWidth * 0.5, cy + p.y - PieMenu::kSlotHeight * 0.5, PieMenu::kSlotWidth, PieMenu::kSlotHeight);
    const bool filled = !menu.entries[static_cast<size_t>(i)].commandId.empty();
    if (!filled) {
      painter.border(box, radii, ctx.px(1.5), ctx.color("muted", 0.55));
      TextOptions number;
      number.align = TextAlign::Center;
      number.color = ctx.color("muted", 0.8);
      ctx.drawText("Slot " + std::to_string(i + 1), muted, box, number);
    }
    if (i == dropSlot_) {
      painter.fillRoundedRect(box, radii, ctx.color("accent", 0.25));
      painter.border(box, radii, ctx.px(2.0), ctx.color("accent"));
    } else if (i == selected_) {
      painter.border(box, radii, ctx.px(2.0), ctx.color("accent"));
    }
  }
  if (focusVisible()) ctx.focusRing(ctx.px(4.0));
}

// ---- selection and editing --------------------------------------------------------------------------

void PiePreviewEditor::say(const std::string& text) const {
  if (onMessage_) onMessage_(text);
}

void PiePreviewEditor::changed() {
  refresh();
  if (onChanged_) onChanged_();
}

void PiePreviewEditor::select(int slot) {
  const cm::MenuDraft* d = draft();
  const int count = d != nullptr ? d->menu().slotCount : 0;
  const int value = slot >= 0 && slot < count ? slot : -1;
  if (value == selected_) return;
  selected_ = value;
  requestPaint();
  if (onSelect_) onSelect_(selected_);
}

bool PiePreviewEditor::dropCommand(int slot, const std::string& commandId) {
  cm::MenuDraft* d = draft();
  if (d == nullptr) return false;
  if (slot < 0 || slot >= d->menu().slotCount) {
    say("Drop the action on one of the slots.");
    return false;
  }
  if (commandId.empty() || services_.registry.find(commandId) == nullptr) {
    say("That action is not available.");
    return false;
  }
  const bool replacing = !d->menu().entries[static_cast<size_t>(slot)].commandId.empty();
  const cm::MenuEditResult result = d->setSlot(static_cast<size_t>(slot), commandId);
  if (!result.ok) {
    say(result.reason.empty() ? "The slot did not accept that action." : result.reason);
    return false;
  }
  const commands::CommandDef* def = services_.registry.find(commandId);
  say("Slot " + std::to_string(slot + 1) + (replacing ? " now runs " : " set to ") + (def != nullptr ? def->label : commandId) + ".");
  select(slot);
  changed();
  return true;
}

bool PiePreviewEditor::clearSlot(int slot) {
  cm::MenuDraft* d = draft();
  if (d == nullptr || slot < 0 || slot >= d->menu().slotCount) return false;
  if (d->menu().entries[static_cast<size_t>(slot)].commandId.empty()) return false;
  const cm::MenuEditResult result = d->clearSlot(static_cast<size_t>(slot));
  if (!result.ok) {
    say(result.reason);
    return false;
  }
  say("Slot " + std::to_string(slot + 1) + " cleared.");
  changed();
  return true;
}

void PiePreviewEditor::openContextMenu(int slot, double x, double y) {
  const cm::MenuDraft* d = draft();
  if (!menu_ || d == nullptr || slot < 0 || slot >= d->menu().slotCount) return;
  MenuSpec spec;
  MenuItemSpec clear = menuAction("clear", "Clear slot " + std::to_string(slot + 1));
  clear.enabled = !d->menu().entries[static_cast<size_t>(slot)].commandId.empty();
  spec.items.push_back(std::move(clear));
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  spec.onCommand = [context, self, slot](const MenuItemSpec& item) {
    if (item.id != "clear") return;
    if (PiePreviewEditor* editor = context->objectAs<PiePreviewEditor>(self)) editor->clearSlot(slot);
  };
  menu_->openContextMenu(std::move(spec), x, y);
}

// ---- pointer and keys -------------------------------------------------------------------------------

void PiePreviewEditor::onPointerDown(Event& e) {
  ui().router().focus(id(), core::events::FocusReason::Pointer);
  const int slot = slotAt(e.x, e.y);
  if (e.button == Button::Right) {
    if (slot >= 0) {
      select(slot);
      openContextMenu(slot, e.x, e.y);
    }
    e.markHandled();
    return;
  }
  if (e.button != Button::Left) return;
  e.markHandled();
  if (slot < 0) return;
  select(slot);
  const cm::MenuDraft* d = draft();
  if (d != nullptr && !d->menu().entries[static_cast<size_t>(slot)].commandId.empty()) {
    pressed_ = slot;
    armed_ = true;
    ui().router().capturePointer(id());
  }
}

void PiePreviewEditor::onDragStart(Event& e) {
  const cm::MenuDraft* d = draft();
  if (!armed_ || dragging_ || hub_ == nullptr || d == nullptr || pressed_ < 0 || pressed_ >= d->menu().slotCount) return;
  const cm::MenuEntry& entry = d->menu().entries[static_cast<size_t>(pressed_)];
  if (entry.commandId.empty()) return;
  DragPayload payload;
  payload.kind = DragPayload::Kind::Node;
  payload.nodeId = kEntryDragPrefix + std::to_string(pressed_);
  const commands::CommandDef* def = services_.registry.find(entry.commandId);
  payload.text = !entry.label.empty() ? entry.label : (def != nullptr ? def->label : entry.commandId);
  dragSource_ = pressed_;
  dragging_ = hub_->begin(std::move(payload), e.x, e.y);
  e.markHandled();
}

void PiePreviewEditor::onPointerMove(Event& e) {
  if (dragging_ && hub_ != nullptr) {
    hub_->move(e.x, e.y);
    e.markHandled();
  }
}

void PiePreviewEditor::onPointerUp(Event& e) {
  if (e.button != Button::Left) return;
  // dragging_ stays set during hub_->end(): the hub asks dragOver once more at the release point and a
  // swap is only accepted while this editor is the source.
  if (dragging_ && hub_ != nullptr) hub_->end(e.x, e.y);
  dragging_ = armed_ = false;
  pressed_ = dragSource_ = -1;
}

void PiePreviewEditor::onCaptureLost(Event&) {
  if (dragging_ && hub_ != nullptr) hub_->cancel();
  dragging_ = armed_ = false;
  pressed_ = dragSource_ = -1;
}

void PiePreviewEditor::onKeyDown(Event& e) {
  const cm::MenuDraft* d = draft();
  if (d == nullptr) return;
  const int count = d->menu().slotCount;
  switch (e.key) {
    case Key::Escape:
      if (dragging_) {
        dragging_ = armed_ = false;
        if (hub_ != nullptr) hub_->cancel();
        ui().router().cancelPointerInteraction();
        e.markHandled();
      }
      return;
    case Key::Delete:
    case Key::Backspace:
      if (selected_ >= 0) clearSlot(selected_);
      break;
    case Key::Right:
    case Key::Down: select(selected_ < 0 ? 0 : (selected_ + 1) % count); break;
    case Key::Left:
    case Key::Up: select(selected_ < 0 ? count - 1 : (selected_ + count - 1) % count); break;
    default: return;
  }
  e.markHandled();
}

// ---- drop target ------------------------------------------------------------------------------------

bool PiePreviewEditor::acceptsPayload(const DragPayload& payload, int slot, int& sourceSlot) {
  sourceSlot = -1;
  cm::MenuDraft* d = draft();
  if (d == nullptr || slot < 0) return false;
  if (payload.kind == DragPayload::Kind::Command) {
    return !payload.commandId.empty() && services_.registry.find(payload.commandId) != nullptr && d->canSetSlot(static_cast<size_t>(slot), payload.commandId).ok;
  }
  int from = -1;
  if (!dragging_ || !parseEntryDrag(payload.nodeId, from) || from != dragSource_ || from == slot) return false;
  sourceSlot = from;
  return d->canMoveEntry(static_cast<size_t>(from), static_cast<size_t>(slot)).ok;
}

bool PiePreviewEditor::dragOver(const DragPayload& payload, double x, double y) {
  const int slot = slotAt(x, y);
  int from = -1;
  const int next = acceptsPayload(payload, slot, from) ? slot : -1;
  if (next != dropSlot_) {
    dropSlot_ = next;
    requestPaint();
  }
  return next >= 0;
}

void PiePreviewEditor::dragLeave() {
  if (dropSlot_ != -1) {
    dropSlot_ = -1;
    requestPaint();
  }
}

bool PiePreviewEditor::dragDrop(const DragPayload& payload, double x, double y) {
  dropSlot_ = -1;
  const int slot = slotAt(x, y);
  if (payload.kind == DragPayload::Kind::Command) return dropCommand(slot, payload.commandId);
  int from = -1;
  cm::MenuDraft* d = draft();
  if (d == nullptr || slot < 0 || !parseEntryDrag(payload.nodeId, from) || from != dragSource_) return false;
  const cm::MenuEditResult result = d->moveEntry(static_cast<size_t>(from), static_cast<size_t>(slot));
  if (!result.ok) {
    say(result.reason);
    return false;
  }
  say("Swapped slots " + std::to_string(from + 1) + " and " + std::to_string(slot + 1) + ".");
  select(slot);
  changed();
  return true;
}

}  // namespace r1ui::widgets
