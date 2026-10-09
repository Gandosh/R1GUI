// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the editing operations of Customization (Customization.h): visibility, move, rename, user
//   menus, user entries, toolbar settings, user toolbars and panels, free-form buttons, resets.
// Invariants: an operation builds a candidate Delta from the user delta, validates it by applying it
//   (tryCommit) and only then replaces the user delta and notifies; a refused operation leaves the
//   state untouched. Generated ids come from Delta::serial, which only grows.
// Callers: the widget layer (edit mode, palette, free-form panel), tests.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

#include "r1ui/commands/Text.h"
#include "r1ui/commands/customize/Customization.h"

namespace r1ui::commands::customize {

namespace {

using Code = ReportEntry::Code;

EditResult failure(EditError error, std::string reason) {
  EditResult r;
  r.error = error;
  r.reason = std::move(reason);
  return r;
}

EditResult success(std::string id = {}) {
  EditResult r;
  r.ok = true;
  r.id = std::move(id);
  return r;
}

// Blocking problems are those that would make the requested change not take effect as asked.
bool blocking(Code code) { return code != Code::MissingCommand; }

EditError errorFor(Code code) {
  switch (code) {
    case Code::Locked: return EditError::Locked;
    case Code::Cycle: return EditError::Cycle;
    case Code::IllegalParent: return EditError::Illegal;
    case Code::MissingParent: return EditError::UnknownParent;
    case Code::MissingNode:
    case Code::MissingAnchor: return EditError::UnknownNode;
    case Code::DuplicateId: return EditError::Duplicate;
    case Code::Invalid: return EditError::OutOfRange;
    case Code::Limit: return EditError::LimitReached;
    default: return EditError::Illegal;
  }
}

std::string reasonFor(const ReportEntry& e) {
  switch (e.code) {
    case Code::Locked: return "That part of the interface is locked by the application.";
    case Code::Cycle: return "An item cannot be moved into itself.";
    case Code::IllegalParent: return "That item cannot go there.";
    case Code::MissingParent: return "The target no longer exists.";
    case Code::MissingAnchor: return "The item next to the drop position no longer exists.";
    case Code::Limit: return "The menu is too large or nested too deeply.";
    default: return e.detail;
  }
}

std::string trimmed(std::string text) {
  const auto space = [](char c) { return c == ' ' || c == '\t'; };
  while (!text.empty() && space(text.back())) text.pop_back();
  size_t first = 0;
  while (first < text.size() && space(text[first])) ++first;
  return text.substr(first);
}

std::string cleanLabel(const std::string& text) { return trimmed(sanitizeText(text, kMaxLabelBytes)); }

std::string lowerAscii(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

void pruneEdit(Delta& d, const std::string& id) {
  const auto it = d.edits.find(id);
  if (it != d.edits.end() && it->second.empty()) d.edits.erase(it);
}

void collectNodeIds(const Node& node, std::unordered_set<std::string>& out) {
  out.insert(node.id);
  for (const Node& c : node.children) collectNodeIds(c, out);
}

}  // namespace

EditResult Customization::tryCommit(Delta candidate, const std::string& subject, EditResult ok) {
  const Report& before = editView().report;
  std::unordered_set<std::string> known;
  for (const ReportEntry& e : before.entries) known.insert(std::to_string(static_cast<int>(e.code)) + "|" + e.id);
  const EffectiveResult after = effectiveLayout(builtin_, merged(candidate), editOptions());
  for (const ReportEntry& e : after.report.entries) {
    if (!blocking(e.code)) continue;
    if (known.count(std::to_string(static_cast<int>(e.code)) + "|" + e.id) != 0) continue;
    (void)subject;
    return failure(errorFor(e.code), reasonFor(e));
  }
  if (candidate == user_) return ok;  // nothing to store (the request matched the current state)
  user_ = std::move(candidate);
  changed();
  return ok;
}

std::string Customization::nextId(Delta& delta, const char* prefix) const {
  for (;;) {
    const std::string id = prefix + std::to_string(++delta.serial);
    if (findNode(builtin_, id) != nullptr || builtin_.menuBar.id == id || findToolbar(builtin_, id) != nullptr || findPanel(builtin_, id) != nullptr) continue;
    const bool used = std::any_of(delta.added.begin(), delta.added.end(), [&](const AddedNode& a) { return a.node.id == id; }) ||
                      std::any_of(delta.userToolbars.begin(), delta.userToolbars.end(), [&](const ToolbarLayout& t) { return t.id == id; }) ||
                      std::any_of(delta.userPanels.begin(), delta.userPanels.end(), [&](const FreeFormPanelLayout& p) { return p.id == id; });
    if (!used) return id;
  }
}

// ---- visibility, order, labels ------------------------------------------------------------------

EditResult Customization::setHidden(const std::string& id, bool hidden) {
  const Node* n = find(id);
  if (n == nullptr) return failure(EditError::UnknownNode, "That entry no longer exists.");
  if (n->locked) return failure(EditError::Locked, lockReason(id));
  bool baseVisible = true;
  if (const Node* b = findNode(builtin_, id)) {
    baseVisible = b->visible;
  } else {
    const auto it = std::find_if(user_.added.begin(), user_.added.end(), [&](const AddedNode& a) { return a.node.id == id; });
    if (it != user_.added.end()) baseVisible = it->node.visible;
  }
  Delta c = user_;
  NodeEdit& edit = c.edits[id];
  if (hidden == !baseVisible) {
    edit.hidden.reset();
  } else {
    edit.hidden = hidden;
  }
  pruneEdit(c, id);
  return tryCommit(std::move(c), id, success(id));
}

EditResult Customization::move(const std::string& id, const Placement& to) {
  const Node* n = find(id);
  if (n == nullptr) return failure(EditError::UnknownNode, "That entry no longer exists.");
  if (n->locked) return failure(EditError::Locked, lockReason(id));
  if (id == to.parent || ((to.side == Side::Before || to.side == Side::After) && to.anchor == id)) {
    return failure(EditError::Cycle, "An item cannot be moved relative to itself.");
  }
  Delta c = user_;
  c.moves.erase(std::remove_if(c.moves.begin(), c.moves.end(), [&](const MoveEdit& m) { return m.node == id; }), c.moves.end());
  c.moves.push_back({id, to});
  return tryCommit(std::move(c), id, success(id));
}

EditResult Customization::renameLabel(const std::string& id, const std::string& label) {
  const Node* n = find(id);
  if (n == nullptr) return failure(EditError::UnknownNode, "That entry no longer exists.");
  if (n->locked) return failure(EditError::Locked, lockReason(id));
  const bool takes = n->kind == Kind::Menu || n->kind == Kind::Section || n->kind == Kind::Heading || n->kind == Kind::Submenu || n->kind == Kind::Command ||
                     n->kind == Kind::FreeButton;
  if (!takes) return failure(EditError::Illegal, "This item has no name to change.");
  const std::string text = cleanLabel(label);
  Delta c = user_;
  NodeEdit& edit = c.edits[id];
  if (text.empty() || text == n->label) {
    edit.label.reset();
  } else {
    if (n->kind == Kind::Menu) {
      for (const Node& other : editView().layout.menuBar.menus) {
        if (other.id != id && lowerAscii(other.shownLabel()) == lowerAscii(text)) return failure(EditError::Duplicate, "Another menu already has that name.");
      }
    }
    edit.label = text;
  }
  pruneEdit(c, id);
  return tryCommit(std::move(c), id, success(id));
}

// ---- user-created nodes -------------------------------------------------------------------------

EditResult Customization::addUserMenu(const std::string& title) {
  const std::string text = cleanLabel(title);
  if (text.empty()) return failure(EditError::InvalidText, "A menu needs a name.");
  const LayoutSet& view = editView().layout;
  if (view.menuBar.locked) return failure(EditError::Locked, "The menu bar is locked by the application.");
  for (const Node& m : view.menuBar.menus) {
    if (lowerAscii(m.shownLabel()) == lowerAscii(text)) return failure(EditError::Duplicate, "A menu with that name already exists.");
  }
  Delta c = user_;
  Node menu = Node::menu(nextId(c, "um"), text);
  menu.user = true;
  Node section = Node::section(nextId(c, "us"), "");
  section.user = true;
  const std::string menuId = menu.id;
  c.added.push_back({std::move(menu), {view.menuBar.id, {}, Side::End}});
  c.added.push_back({std::move(section), {menuId, {}, Side::End}});
  return tryCommit(std::move(c), menuId, success(menuId));
}

void Customization::removeSubtree(Delta& d, const std::string& id) const {
  std::unordered_set<std::string> ids{id};
  for (const AddedNode& a : d.added) {
    if (ids.count(a.at.parent) != 0) ids.insert(a.node.id);
  }
  d.added.erase(std::remove_if(d.added.begin(), d.added.end(), [&](const AddedNode& a) { return ids.count(a.node.id) != 0; }), d.added.end());
  for (const std::string& gone : ids) d.edits.erase(gone);
  d.moves.erase(std::remove_if(d.moves.begin(), d.moves.end(), [&](const MoveEdit& m) { return ids.count(m.node) != 0 || ids.count(m.to.parent) != 0; }), d.moves.end());
  d.userToolbars.erase(std::remove_if(d.userToolbars.begin(), d.userToolbars.end(), [&](const ToolbarLayout& t) { return t.id == id; }), d.userToolbars.end());
  d.userPanels.erase(std::remove_if(d.userPanels.begin(), d.userPanels.end(), [&](const FreeFormPanelLayout& p) { return p.id == id; }), d.userPanels.end());
  d.toolbarEdits.erase(id);
  d.panelEdits.erase(id);
}

EditResult Customization::removeUserEntry(const std::string& id) {
  const Node* n = find(id);
  if (n == nullptr) return failure(EditError::UnknownNode, "That entry no longer exists.");
  if (!n->user) return failure(EditError::NotUserNode, "Built-in entries cannot be removed; hide them instead.");
  if (n->locked) return failure(EditError::Locked, lockReason(id));
  Delta c = user_;
  removeSubtree(c, id);
  return tryCommit(std::move(c), id, success(id));
}

EditResult Customization::deleteUserMenu(const std::string& id) {
  const Node* n = find(id);
  if (n == nullptr) return failure(EditError::UnknownNode, "That menu no longer exists.");
  if (n->kind != Kind::Menu) return failure(EditError::Illegal, "That is not a menu.");
  if (!n->user) return failure(EditError::NotUserNode, "Built-in menus cannot be deleted; hide them instead.");
  return removeUserEntry(id);
}

EditResult Customization::addUserNode(const std::string& parent, Node node, const std::string& anchor, Side side, std::vector<std::string> groupCommands) {
  const LayoutSet& view = editView().layout;
  Delta c = user_;
  Placement at{parent, anchor, side};
  const Node* p = find(parent);
  bool container = false;
  Kind parentKind = Kind::MenuBar;
  if (p != nullptr) {
    parentKind = p->kind;
  } else if (parent == view.menuBar.id) {
    container = true;
  } else if (findToolbar(view, parent) != nullptr) {
    container = true;
    parentKind = Kind::Toolbar;
  } else if (findPanel(view, parent) != nullptr) {
    container = true;
    parentKind = Kind::Panel;
  } else {
    return failure(EditError::UnknownParent, "The target no longer exists.");
  }
  if (container && parent == view.menuBar.id) parentKind = Kind::MenuBar;
  if (isLocked(parent)) return failure(EditError::Locked, lockReason(parent));

  // Entries dropped on a menu or submenu go to its first section (one is created for a menu without).
  const bool entry = node.kind == Kind::Command || node.kind == Kind::Separator || node.kind == Kind::Heading || node.kind == Kind::Submenu;
  if (entry && (parentKind == Kind::Menu || parentKind == Kind::Submenu)) {
    if (!p->children.empty()) {
      at = {p->children.front().id, anchor, side};
    } else {
      Node section = Node::section(nextId(c, "us"), "");
      section.user = true;
      at = {section.id, {}, Side::End};  // a new section is empty: no anchor
      c.added.push_back({std::move(section), {parent, {}, Side::End}});
    }
  }
  node.user = true;
  node.id = nextId(c, "u");
  node.locked = false;
  const std::string id = node.id;
  const bool submenu = node.kind == Kind::Submenu;
  const bool group = node.kind == Kind::Group;
  c.added.push_back({std::move(node), at});
  if (submenu) {
    Node section = Node::section(nextId(c, "us"), "");
    section.user = true;
    c.added.push_back({std::move(section), {id, {}, Side::End}});
  }
  if (group) {
    for (const std::string& commandId : groupCommands) {
      Node command = Node::command(nextId(c, "u"), commandId);
      command.user = true;
      c.added.push_back({std::move(command), {id, {}, Side::End}});
    }
  }
  return tryCommit(std::move(c), id, success(id));
}

EditResult Customization::addCommand(const std::string& parent, const std::string& commandId, const std::string& anchor, Side side) {
  if (!isValidIdentifier(commandId, kMaxIdBytes)) return failure(EditError::UnknownCommand, "That is not a command.");
  if (exists_ && !exists_(commandId)) return failure(EditError::UnknownCommand, "That command is not available.");
  const LayoutSet& view = editView().layout;
  if (findPanel(view, parent) != nullptr) return failure(EditError::Illegal, "Place buttons on a free-form panel by position.");
  Node node = Node::command("", commandId);
  return addUserNode(parent, std::move(node), anchor, side);
}

EditResult Customization::addSeparator(const std::string& parent, const std::string& anchor, Side side) {
  return addUserNode(parent, Node::separator(""), anchor, side);
}

EditResult Customization::addHeading(const std::string& parent, const std::string& text, const std::string& anchor, Side side) {
  const std::string clean = cleanLabel(text);
  if (clean.empty()) return failure(EditError::InvalidText, "A heading needs text.");
  return addUserNode(parent, Node::heading("", clean), anchor, side);
}

EditResult Customization::addSubmenu(const std::string& parent, const std::string& title, const std::string& anchor, Side side) {
  const std::string clean = cleanLabel(title);
  if (clean.empty()) return failure(EditError::InvalidText, "A sub-menu needs a name.");
  return addUserNode(parent, Node::submenu("", clean), anchor, side);
}

EditResult Customization::addSection(const std::string& menuOrSubmenu, const std::string& heading, const std::string& anchor, Side side) {
  return addUserNode(menuOrSubmenu, Node::section("", cleanLabel(heading)), anchor, side);
}

EditResult Customization::addSpacer(const std::string& toolbar, const std::string& anchor, Side side) {
  return addUserNode(toolbar, Node::spacer(""), anchor, side);
}

EditResult Customization::addGroup(const std::string& toolbar, const std::vector<std::string>& commandIds, const std::string& anchor, Side side) {
  if (commandIds.empty()) return failure(EditError::Illegal, "A group needs at least one command.");
  for (const std::string& id : commandIds) {
    if (!isValidIdentifier(id, kMaxIdBytes) || (exists_ && !exists_(id))) return failure(EditError::UnknownCommand, "That command is not available.");
  }
  return addUserNode(toolbar, Node::group("", {}), anchor, side, commandIds);
}

// ---- resets -------------------------------------------------------------------------------------

EditResult Customization::resetMenu(const std::string& id) {
  const LayoutSet& view = editView().layout;
  std::unordered_set<std::string> inBuiltin, inView;
  bool userContainer = false;
  bool isToolbar = false, isPanel = false;
  if (const Node* b = findNode(builtin_, id)) {
    if (b->kind != Kind::Menu) return failure(EditError::Illegal, "Only a whole menu, toolbar or panel can be reset.");
    collectNodeIds(*b, inBuiltin);
  } else if (const ToolbarLayout* t = findToolbar(builtin_, id)) {
    isToolbar = true;
    for (const Node& n : t->items) collectNodeIds(n, inBuiltin);
  } else if (const FreeFormPanelLayout* p = findPanel(builtin_, id)) {
    isPanel = true;
    for (const Node& n : p->buttons) collectNodeIds(n, inBuiltin);
  } else {
    const Node* n = find(id);
    if (n != nullptr && n->kind == Kind::Menu) {
      userContainer = true;
    } else if (findToolbar(view, id) != nullptr) {
      userContainer = true;
      isToolbar = true;
    } else if (findPanel(view, id) != nullptr) {
      userContainer = true;
      isPanel = true;
    } else {
      return failure(EditError::UnknownNode, "That menu no longer exists.");
    }
  }
  if (isLocked(id)) return failure(EditError::Locked, lockReason(id));
  if (const Node* n = find(id)) {
    collectNodeIds(*n, inView);
  } else if (const ToolbarLayout* t = findToolbar(view, id)) {
    for (const Node& n2 : t->items) collectNodeIds(n2, inView);
  } else if (const FreeFormPanelLayout* p = findPanel(view, id)) {
    for (const Node& n2 : p->buttons) collectNodeIds(n2, inView);
  }
  inView.insert(id);
  inBuiltin.insert(id);
  Delta c = user_;
  for (const std::string& gone : inBuiltin) c.edits.erase(gone);
  if (userContainer) {
    for (const std::string& gone : inView) c.edits.erase(gone);
  }
  c.moves.erase(std::remove_if(c.moves.begin(), c.moves.end(),
                               [&](const MoveEdit& m) {
                                 return inBuiltin.count(m.node) != 0 || inView.count(m.node) != 0 || inBuiltin.count(m.to.parent) != 0 || inView.count(m.to.parent) != 0;
                               }),
                c.moves.end());
  if (!userContainer) {
    // User entries added to a built-in menu go with the reset (the ones created inside it).
    std::unordered_set<std::string> doomed;
    for (const AddedNode& a : c.added) {
      if (inBuiltin.count(a.at.parent) != 0 || doomed.count(a.at.parent) != 0) doomed.insert(a.node.id);
    }
    c.added.erase(std::remove_if(c.added.begin(), c.added.end(), [&](const AddedNode& a) { return doomed.count(a.node.id) != 0; }), c.added.end());
    for (const std::string& gone : doomed) c.edits.erase(gone);
  }
  if (isToolbar) c.toolbarEdits.erase(id);
  if (isPanel) c.panelEdits.erase(id);
  return tryCommit(std::move(c), id, success(id));
}

EditResult Customization::resetAll() {
  Delta c;
  c.serial = user_.serial;
  return tryCommit(std::move(c), {}, success());
}

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
