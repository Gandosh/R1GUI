// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the inline rename lifecycle of TreeView: beginRename (F2, the slow click, the API), commit with
//   the application's validation, cancel, and arming the slow-click timer.
// Invariants: at most one rename is open; the renamed node exists in the model while it is open (a rebuild
//   closes it otherwise); a refused commit keeps the field open with the error.
// Callers: TreeView input handlers, the timer in advance(), applications.
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/tree/TreeView.h"

namespace r1ui::widgets {

namespace layout = core::layout;
using core::events::Button;
using core::events::Key;
using core::events::Phase;
namespace Mod = core::events::Mod;

// ---- rename -------------------------------------------------------------------------------------

bool TreeView::beginRename(NodeId node) {
  ensureRows();
  if (!model_ || drag_.active || !enabled()) return false;
  const auto it = rowIndex_.find(node);
  if (it == rowIndex_.end() || !model_->flags(node).renamable) return false;
  if (rename_.active()) cancelRename();
  slow_.armed = false;
  scrollToNode(node, false);
  rename_.begin(ui(), node, model_->label(node));
  renameRow_ = it->second;
  ui().router().focus(id(), core::events::FocusReason::Program);
  clampScroll();
  scrollToNode(node, false);
  requestPaint();
  return true;
}

bool TreeView::commitRename() {
  if (!rename_.active()) return false;
  const core::tree::WidgetId self = id();
  const NodeId node = rename_.node();
  const std::string text = rename_.text();
  if (text.empty()) {
    rename_.setError("The name cannot be empty");
    requestPaint();
    return false;
  }
  if (text != labelOf(node) && onRename_) {
    auto cb = onRename_;
    const RenameResult result = cb(node, text);
    if (!ui().alive(self)) return true;
    if (!result.ok) {
      rename_.setError(result.error.empty() ? std::string("Invalid name") : result.error);
      requestPaint();
      return false;
    }
  }
  rename_.end();
  renameRow_.reset();
  clampScroll();
  requestPaint();
  return true;
}

void TreeView::cancelRename() {
  if (!rename_.active()) return;
  rename_.end();
  renameRow_.reset();
  clampScroll();
  requestPaint();
}

void TreeView::armRename(NodeId node) {
  slow_.armed = true;
  slow_.node = node;
  slow_.dueMs = ui().now() + kSlowClickMs;
  wantFrames(true);
}

}  // namespace r1ui::widgets
