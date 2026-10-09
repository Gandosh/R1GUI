// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: drag and drop of TreeView rows (payload, drop zones above / below / onto, validation through the
//   application's handlers, the drop itself) and the time step that drives edge auto-scroll and the
//   slow-click rename timer.
// Invariants: a drag exists only between an accepted DragStart and the release or cancel; the payload
//   holds NodeIds in row order without descendants of other payload nodes; the drop request is built from
//   the preview computed on the last pointer sample.
// Callers: TreeView input handlers, the step timer (advance), tests (advance).
#include <algorithm>
#include <cmath>

#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace r1ui::widgets {

namespace layout = core::layout;
using core::events::Button;
using core::events::Key;
using core::events::Phase;
namespace Mod = core::events::Mod;

namespace {
constexpr uint64_t kStepMs = 16;     // period of the timer steps
constexpr uint64_t kMaxStepMs = 50;  // a long gap between frames must not make the list jump
constexpr double kDropBandFraction = 0.25;
constexpr double kDropBandMin = 3.0;
constexpr double kDropBandMax = 10.0;
}  // namespace

// ---- drag and drop ------------------------------------------------------------------------------

std::vector<NodeId> TreeView::dragPayload() {
  std::vector<size_t> selectedRows;
  for (const NodeId n : selected_) {
    const auto it = rowIndex_.find(n);
    if (it != rowIndex_.end() && model_->flags(n).draggable) selectedRows.push_back(it->second);
  }
  std::sort(selectedRows.begin(), selectedRows.end());
  std::vector<NodeId> payload;
  size_t coveredUntil = 0;  // rows below a dragged node travel with it
  for (const size_t r : selectedRows) {
    if (r < coveredUntil) continue;
    payload.push_back(rows_[r].id);
    size_t end = r + 1;
    while (end < rows_.size() && rows_[end].depth > rows_[r].depth) ++end;
    coveredUntil = end;
  }
  return payload;
}

void TreeView::onDragStart(Event& e) {
  if (!drag_.armed || drag_.active || mode_ == TreeSelectionMode::None || rename_.active() || barDragging_) return;
  slow_.armed = false;
  ensureRows();
  const auto pressedRow = rowIndex_.find(drag_.pressed);
  if (pressedRow == rowIndex_.end() || !model_->flags(drag_.pressed).draggable) return;
  if (selected_.count(drag_.pressed) == 0) return;
  drag_.payload = dragPayload();
  if (drag_.payload.empty()) return;
  drag_.active = true;
  drag_.collapseOnRelease = false;
  drag_.pointerX = e.x;
  drag_.pointerY = e.y;
  hover_ = {};
  e.markHandled();
  updateDrop(e.x, e.y);
  wantFrames(true);
  requestPaint();
}

bool TreeView::dropAllowed(NodeId target, DropZone zone) {
  const auto it = rowIndex_.find(target);
  if (it == rowIndex_.end() || drag_.payload.empty()) return false;
  const size_t t = it->second;
  // A node cannot be dropped relative to itself or into its own subtree.
  for (const NodeId p : drag_.payload) {
    const auto payloadRow = rowIndex_.find(p);
    if (payloadRow == rowIndex_.end()) return false;  // its row went away (collapsed parent): nothing can be dropped
    const size_t r = payloadRow->second;
    size_t end = r + 1;
    while (end < rows_.size() && rows_[end].depth > rows_[r].depth) ++end;
    if (t >= r && t < end) return false;
  }
  if (zone == DropZone::Onto && !model_->flags(target).acceptsDrops) return false;
  if (acceptDrop_) return acceptDrop_(buildRequest(target, zone));
  return true;
}

DropRequest TreeView::buildRequest(NodeId target, DropZone zone) const {
  DropRequest req;
  req.nodes = drag_.payload;
  req.target = target;
  req.zone = zone;
  return req;
}

void TreeView::updateDrop(double x, double y) {
  (void)x;
  drag_.preview = {};
  ensureRows();
  if (rows_.empty()) return;
  const layout::Rect a = absOrigin();
  const double ly = y - a.y;
  size_t row;
  DropZone zone;
  const auto at = rowAtLocalY(ly);
  if (!at) {
    // Past the last row: below it; above the first row: above it.
    if (ly < 0.0) {
      row = 0;
      zone = DropZone::Above;
    } else {
      row = rows_.size() - 1;
      zone = DropZone::Below;
    }
  } else {
    row = *at;
    const double height = rowHeightOf(row);
    const double inRow = ly - rowTopLocal(row);
    const double band = std::clamp(height * kDropBandFraction, kDropBandMin, kDropBandMax);
    if (inRow < band) zone = DropZone::Above;
    else if (inRow >= height - band) zone = DropZone::Below;
    else zone = DropZone::Onto;
    if (zone == DropZone::Onto && !model_->flags(rows_[row].id).acceptsDrops) zone = inRow < height * 0.5 ? DropZone::Above : DropZone::Below;
  }
  const NodeId target = rows_[row].id;
  if (dropAllowed(target, zone)) drag_.preview = {true, target, zone};
  requestPaint();
}

void TreeView::finishDrag(bool commit) {
  if (!drag_.active) return;
  const core::tree::WidgetId self = id();
  const DropPreview preview = drag_.preview;
  const std::vector<NodeId> payload = drag_.payload;
  drag_ = {};
  wantFrames(false);
  requestPaint();
  if (!commit || !preview.valid || !onDrop_) return;
  DropRequest req;
  req.nodes = payload;
  req.target = preview.target;
  req.zone = preview.zone;
  auto cb = onDrop_;
  cb(req);
  if (!ui().alive(self)) return;
  rebuildRows();
  if (req.zone == DropZone::Onto && model_ && model_->contains(req.target)) setExpanded(req.target, true);
}

void TreeView::cancelDrag() {
  const bool was = drag_.active;
  if (was) finishDrag(false);
  drag_ = {};
  if (was) ui().router().cancelPointerInteraction();
}

// ---- time ---------------------------------------------------------------------------------------

// The time steps (edge auto-scroll, the slow-click rename) run from a context timer, not from paint:
// they change scroll and focus and may call the application, none of which belongs in the paint walk.
void TreeView::wantFrames(bool on) {
  framesWanted_ = on;
  if (!on) {
    if (stepTimer_ != 0) ui().cancelTimer(stepTimer_);
    stepTimer_ = 0;
    return;
  }
  if (stepTimer_ != 0) return;
  const core::tree::WidgetId self = id();
  UiContext* context = &ui();
  stepTimer_ = context->setTimer(kStepMs, [context, self]() {
    if (TreeView* tree = context->objectAs<TreeView>(self)) {
      tree->stepTimer_ = 0;
      tree->advance(context->now());
    }
  });
}

void TreeView::advance(uint64_t now) {
  const uint64_t dt = lastAdvanceMs_ != 0 && now > lastAdvanceMs_ ? std::min(now - lastAdvanceMs_, kMaxStepMs) : 0;
  lastAdvanceMs_ = now;
  bool frames = false;
  if (slow_.armed) {
    if (now >= slow_.dueMs) {
      const NodeId node = slow_.node;
      slow_.armed = false;
      beginRename(node);
    } else {
      frames = true;
    }
  }
  if (drag_.active) {
    const layout::Rect a = absOrigin();
    const double top = a.y;
    const double bottom = static_cast<double>(a.y) + a.h;
    double direction = 0.0;
    double depth = 0.0;
    if (drag_.pointerY < top + kEdgeScrollZone) {
      direction = -1.0;
      depth = (top + kEdgeScrollZone - drag_.pointerY) / kEdgeScrollZone;
    } else if (drag_.pointerY > bottom - kEdgeScrollZone) {
      direction = 1.0;
      depth = (drag_.pointerY - (bottom - kEdgeScrollZone)) / kEdgeScrollZone;
    }
    if (direction != 0.0) {
      frames = true;
      const double speed = kEdgeScrollMax * std::clamp(depth, 0.0, 1.0);  // up to 600 px/s, reached at the edge and beyond
      const double before = scroll_;
      applyScroll(scroll_ + direction * speed * static_cast<double>(dt) / 1000.0);
      if (scroll_ != before) updateDrop(drag_.pointerX, drag_.pointerY);
    }
  }
  wantFrames(frames);
}

}  // namespace r1ui::widgets
