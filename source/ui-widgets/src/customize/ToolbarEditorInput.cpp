// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ToolbarEditor.h, part 2: the strip's pointer and keyboard interaction, the actions
//   behind them (hide, remove, move by key), the command picker, the context menu and the drop logic (the
//   DragTarget side).
// Invariants: a drag always ends through the hub (release, Escape, capture loss or destruction); the
//   indicator is shown only where the model accepts the change; handlers never touch members after a
//   call that can rebuild the strip.
// Callers: the UiContext (events), DragHub, tests.
#include <algorithm>
#include <cmath>

#include "CustomizeCommon.h"
#include "ToolbarEditorMetrics.h"
#include "r1ui/widgets/customize/CommandPicker.h"
#include "r1ui/widgets/customize/ToolbarEditor.h"

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
    if (vertical_) return {self.x + kStripPad, self.y + at - 1.0, step_, 2.0};
    return {self.x + at - 1.0, self.y + kStripBand + kStripPad, 2.0, step_};
  };
  if (cells_.empty()) {
    c.placement = {toolbarId_, "", cz::Side::End};
    c.line = lineAt(kStripPad);
    return c;
  }
  for (size_t i = 0; i < cells_.size(); ++i) {
    const Cell& cell = cells_[i];
    const double end = i + 1 < cells_.size() ? cells_[i + 1].pos - half : cell.pos + cell.size + kStripPad;
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
  const cz::EditResult r = controller_.model().preview([&](cz::Customization& m) { return runStripDrop(m, payload, c.placement); });
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
  const cz::EditResult r = runStripDrop(controller_.model(), payload, c.placement);
  controller_.noteResult(r);
  return r.ok;
}

}  // namespace r1ui::widgets
