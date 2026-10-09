// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of FreeFormPanel.h, part 3: the FreeFormCanvas interaction in edit mode: pointer
//   (selection rules, move, resize with eight handles, marquee), keyboard (nudge, delete, select all,
//   Escape, Insert), the actions behind them (nudge, delete, align), the command picker, the context menu
//   and the palette drops (the DragTarget side).
// Invariants: a drag commits on release only if it moved past the threshold, clearing the provisional
//   rectangles first so the rebuild a commit triggers never sees stale previews; every geometry change
//   goes through Customization.
// Callers: the UiContext (events), DragHub, tests.
#include <algorithm>
#include <cmath>

#include "CustomizeCommon.h"
#include "FreeFormCommon.h"
#include "r1ui/widgets/customize/CommandPicker.h"
#include "r1ui/widgets/customize/FreeFormPanel.h"

namespace r1ui::widgets {

namespace cz = commands::customize;
namespace layout = core::layout;
using MouseButton = core::events::Button;
using core::events::Key;
namespace Mod = core::events::Mod;
using cust::intersects;
using cust::normalized;
using cust::same;
using cust::toC;
using cust::toD;

namespace {

constexpr double kDragThreshold = 5.0;

}  // namespace

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

}  // namespace r1ui::widgets
