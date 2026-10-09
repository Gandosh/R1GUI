// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the editing operations of Customization (Customization.h), part 2: toolbar size step and gap, user
//   toolbars, user panels, snap and grid, and the free-form button operations (place, rectangle, move,
//   resize, delete, z-order, fitRect).
// Invariants: every rectangle is fitted (snap, minimum size, clamp inside the panel) before it is stored;
//   an operation that matches the current state leaves the delta unchanged (no stray edit entries).
// Callers: the widget layer (toolbar editor, free-form panel), tests.
#include <algorithm>
#include <cmath>

#include "CustomizationEditSupport.h"
#include "r1ui/commands/Text.h"
#include "r1ui/commands/customize/Customization.h"

namespace r1ui::commands::customize {

using namespace detail;

// ---- toolbars -----------------------------------------------------------------------------------

const ToolbarLayout* Customization::baseToolbar(const std::string& id) const {
  if (const ToolbarLayout* t = findToolbar(builtin_, id)) return t;
  const auto it = std::find_if(user_.userToolbars.begin(), user_.userToolbars.end(), [&](const ToolbarLayout& t) { return t.id == id; });
  return it == user_.userToolbars.end() ? nullptr : &*it;
}

const FreeFormPanelLayout* Customization::basePanel(const std::string& id) const {
  if (const FreeFormPanelLayout* p = findPanel(builtin_, id)) return p;
  const auto it = std::find_if(user_.userPanels.begin(), user_.userPanels.end(), [&](const FreeFormPanelLayout& p) { return p.id == id; });
  return it == user_.userPanels.end() ? nullptr : &*it;
}

EditResult Customization::addUserToolbar(const std::string& title, Orientation orientation) {
  const std::string text = cleanLabel(title);
  if (text.empty()) return failure(EditError::InvalidText, "A toolbar needs a name.");
  const LayoutSet& view = editView().layout;
  for (const ToolbarLayout& t : view.toolbars) {
    if (lowerAscii(t.title) == lowerAscii(text)) return failure(EditError::Duplicate, "A toolbar with that name already exists.");
  }
  Delta c = user_;
  ToolbarLayout t;
  t.id = nextId(c, "ut");
  t.title = text;
  t.user = true;
  t.orientation = orientation;
  const std::string id = t.id;
  c.userToolbars.push_back(std::move(t));
  return tryCommit(std::move(c), id, success(id));
}

EditResult Customization::deleteUserToolbar(const std::string& id) {
  const ToolbarLayout* t = findToolbar(editView().layout, id);
  if (t == nullptr) return failure(EditError::UnknownNode, "That toolbar no longer exists.");
  if (!t->user) return failure(EditError::NotUserNode, "Built-in toolbars cannot be deleted.");
  Delta c = user_;
  removeSubtree(c, id);
  return tryCommit(std::move(c), id, success(id));
}

EditResult Customization::setToolbarSizeStep(const std::string& toolbar, SizeStep step) {
  const ToolbarLayout* t = findToolbar(editView().layout, toolbar);
  const ToolbarLayout* base = baseToolbar(toolbar);
  if (t == nullptr || base == nullptr) return failure(EditError::UnknownNode, "That toolbar no longer exists.");
  if (t->locked) return failure(EditError::Locked, "This toolbar is locked by the application.");
  Delta c = user_;
  ToolbarEdit& edit = c.toolbarEdits[toolbar];
  if (step == base->sizeStep) {
    edit.sizeStep.reset();
  } else {
    edit.sizeStep = step;
  }
  if (!edit.sizeStep && !edit.gap) c.toolbarEdits.erase(toolbar);
  return tryCommit(std::move(c), toolbar, success(toolbar));
}

EditResult Customization::setToolbarGap(const std::string& toolbar, double gap) {
  if (!std::isfinite(gap) || gap < kMinToolbarGap || gap > kMaxToolbarGap) return failure(EditError::OutOfRange, "The gap must be between 0 and 32 pixels.");
  const ToolbarLayout* t = findToolbar(editView().layout, toolbar);
  const ToolbarLayout* base = baseToolbar(toolbar);
  if (t == nullptr || base == nullptr) return failure(EditError::UnknownNode, "That toolbar no longer exists.");
  if (t->locked) return failure(EditError::Locked, "This toolbar is locked by the application.");
  Delta c = user_;
  ToolbarEdit& edit = c.toolbarEdits[toolbar];
  if (gap == base->gap) {
    edit.gap.reset();
  } else {
    edit.gap = gap;
  }
  if (!edit.sizeStep && !edit.gap) c.toolbarEdits.erase(toolbar);
  return tryCommit(std::move(c), toolbar, success(toolbar));
}

// ---- free-form panels ---------------------------------------------------------------------------

EditResult Customization::addUserPanel(const std::string& title, double width, double height) {
  const std::string text = cleanLabel(title);
  if (text.empty()) return failure(EditError::InvalidText, "A panel needs a name.");
  const double lowest = kMinButtonSize * 2.0;
  if (!std::isfinite(width) || !std::isfinite(height) || width < lowest || height < lowest || width > kMaxPanelSize || height > kMaxPanelSize) {
    return failure(EditError::OutOfRange, "The panel size is out of range.");
  }
  Delta c = user_;
  FreeFormPanelLayout p;
  p.id = nextId(c, "up");
  p.title = text;
  p.user = true;
  p.width = width;
  p.height = height;
  const std::string id = p.id;
  c.userPanels.push_back(std::move(p));
  return tryCommit(std::move(c), id, success(id));
}

EditResult Customization::deleteUserPanel(const std::string& id) {
  const FreeFormPanelLayout* p = findPanel(editView().layout, id);
  if (p == nullptr) return failure(EditError::UnknownNode, "That panel no longer exists.");
  if (!p->user) return failure(EditError::NotUserNode, "Built-in panels cannot be deleted.");
  Delta c = user_;
  removeSubtree(c, id);
  return tryCommit(std::move(c), id, success(id));
}

EditResult Customization::setPanelSnap(const std::string& panel, bool snap, double grid) {
  if (!std::isfinite(grid) || grid < 1.0 || grid > 256.0) return failure(EditError::OutOfRange, "The grid must be between 1 and 256 pixels.");
  const FreeFormPanelLayout* p = findPanel(editView().layout, panel);
  const FreeFormPanelLayout* base = basePanel(panel);
  if (p == nullptr || base == nullptr) return failure(EditError::UnknownNode, "That panel no longer exists.");
  if (p->locked) return failure(EditError::Locked, "This panel is locked by the application.");
  Delta c = user_;
  PanelEdit& edit = c.panelEdits[panel];
  if (snap == base->snap) {
    edit.snap.reset();
  } else {
    edit.snap = snap;
  }
  if (grid == base->grid) {
    edit.grid.reset();
  } else {
    edit.grid = grid;
  }
  if (!edit.snap && !edit.grid) c.panelEdits.erase(panel);
  return tryCommit(std::move(c), panel, success(panel));
}

Rect Customization::fitRect(const std::string& panel, Rect rect) const {
  const FreeFormPanelLayout* p = findPanel(editView().layout, panel);
  if (p == nullptr) return rect;
  const double minSize = kMinButtonSize;
  const auto fitAxis = [&](double& pos, double& size, double whole) {
    if (!std::isfinite(pos)) pos = 0.0;
    if (!std::isfinite(size)) size = minSize;
    size = std::clamp(size, minSize, std::max(minSize, whole));
    if (p->snap) {
      const double g = p->grid;
      size = std::max(std::ceil(minSize / g) * g, std::round(size / g) * g);
      if (size > whole) size = std::max(minSize, std::floor(whole / g) * g);
      pos = std::round(pos / g) * g;
    }
    pos = std::clamp(pos, 0.0, std::max(0.0, whole - size));
  };
  fitAxis(rect.x, rect.w, p->width);
  fitAxis(rect.y, rect.h, p->height);
  return rect;
}

const FreeFormPanelLayout* Customization::panelOfButton(const std::string& id) const {
  for (const FreeFormPanelLayout& p : editView().layout.panels) {
    for (const Node& b : p.buttons) {
      if (b.id == id) return &p;
    }
  }
  return nullptr;
}

EditResult Customization::placeButton(const std::string& panel, const std::string& commandId, Rect rect) {
  if (!isValidIdentifier(commandId, kMaxIdBytes)) return failure(EditError::UnknownCommand, "That is not a command.");
  if (exists_ && !exists_(commandId)) return failure(EditError::UnknownCommand, "That command is not available.");
  const FreeFormPanelLayout* p = findPanel(editView().layout, panel);
  if (p == nullptr) return failure(EditError::UnknownParent, "That panel no longer exists.");
  if (p->locked) return failure(EditError::Locked, "This panel is locked by the application.");
  if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) || !std::isfinite(rect.h)) {
    return failure(EditError::OutOfRange, "The position is not a number.");
  }
  const Rect fitted = fitRect(panel, rect);
  Delta c = user_;
  Node button = Node::freeButton(nextId(c, "uf"), commandId, fitted);
  button.user = true;
  const std::string id = button.id;
  c.added.push_back({std::move(button), {panel, {}, Side::End}});
  EditResult r = tryCommit(std::move(c), id, success(id));
  r.rect = fitted;
  return r;
}

EditResult Customization::setButtonRect(const std::string& id, Rect rect) {
  const Node* n = find(id);
  const FreeFormPanelLayout* p = panelOfButton(id);
  if (n == nullptr || p == nullptr) return failure(EditError::UnknownNode, "That button no longer exists.");
  if (n->locked || p->locked) return failure(EditError::Locked, lockReason(id).empty() ? "This panel is locked by the application." : lockReason(id));
  if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) || !std::isfinite(rect.h)) {
    return failure(EditError::OutOfRange, "The position is not a number.");
  }
  const Rect fitted = fitRect(p->id, rect);
  Delta c = user_;
  NodeEdit& edit = c.edits[id];
  const Node* base = findNode(builtin_, id);
  if (base != nullptr && base->rect == fitted) {
    edit.rect.reset();
  } else {
    edit.rect = fitted;
  }
  pruneEdit(c, id);
  EditResult r = tryCommit(std::move(c), id, success(id));
  r.rect = fitted;
  return r;
}

EditResult Customization::moveButton(const std::string& id, double x, double y) {
  const Node* n = find(id);
  if (n == nullptr) return failure(EditError::UnknownNode, "That button no longer exists.");
  Rect r = n->rect;
  r.x = x;
  r.y = y;
  return setButtonRect(id, r);
}

EditResult Customization::resizeButton(const std::string& id, double width, double height) {
  const Node* n = find(id);
  if (n == nullptr) return failure(EditError::UnknownNode, "That button no longer exists.");
  Rect r = n->rect;
  r.w = width;
  r.h = height;
  return setButtonRect(id, r);
}

EditResult Customization::deleteButton(const std::string& id) {
  const Node* n = find(id);
  if (n == nullptr) return failure(EditError::UnknownNode, "That button no longer exists.");
  return n->user ? removeUserEntry(id) : setHidden(id, true);
}

EditResult Customization::bringToFront(const std::string& id) {
  const FreeFormPanelLayout* p = panelOfButton(id);
  if (p == nullptr) return failure(EditError::UnknownNode, "That button no longer exists.");
  return move(id, {p->id, {}, Side::End});
}

EditResult Customization::sendToBack(const std::string& id) {
  const FreeFormPanelLayout* p = panelOfButton(id);
  if (p == nullptr) return failure(EditError::UnknownNode, "That button no longer exists.");
  return move(id, {p->id, {}, Side::Start});
}

}  // namespace r1ui::commands::customize
