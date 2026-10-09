// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the state, views, queries, listeners and edit session of Customization (Customization.h).
//   The editing operations live in CustomizationEdit.cpp.
// Invariants: version() changes with every change of an input (builtin, delta layer, command set) and
//   the cached views are recomputed lazily for the version they were built for; listeners are called
//   after the state is consistent and never while another notification is running.
// Callers: the widget layer, hosts, tests.
#include "r1ui/commands/customize/Customization.h"

#include <algorithm>
#include <functional>

namespace r1ui::commands::customize {

Customization::Customization(LayoutSet builtin, CommandExists exists) : builtin_(std::move(builtin)), exists_(std::move(exists)) {}

void Customization::changed() {
  ++version_;
  normal_.reset();
  edit_.reset();
  if (notifying_) return;
  notifying_ = true;
  const auto snapshot = listeners_;
  for (const auto& entry : snapshot) {
    if (entry.second) entry.second();
  }
  notifying_ = false;
}

void Customization::setBuiltin(LayoutSet builtin) {
  builtin_ = std::move(builtin);
  changed();
}

void Customization::setCommandExists(CommandExists exists) {
  exists_ = std::move(exists);
  changed();
}

void Customization::setUserDelta(Delta delta) {
  user_ = std::move(delta);
  changed();
}

void Customization::setWorkspaceDelta(Delta delta) {
  workspace_ = std::move(delta);
  changed();
}

Customization::ListenerId Customization::subscribe(Listener listener) {
  const ListenerId id = nextListener_++;
  listeners_.emplace_back(id, std::move(listener));
  return id;
}

void Customization::unsubscribe(ListenerId id) {
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [id](const auto& e) { return e.first == id; }), listeners_.end());
}

// ---- views --------------------------------------------------------------------------------------

Delta Customization::merged(const Delta& user) const { return workspace_.empty() ? user : mergeDeltas(user, workspace_); }

EffectiveOptions Customization::editOptions() const {
  EffectiveOptions o;
  o.keepHidden = true;
  o.keepMissing = true;
  o.exists = exists_;
  return o;
}

const EffectiveResult& Customization::effective() const {
  if (!normal_) {
    EffectiveOptions o;
    o.exists = exists_;
    normal_ = effectiveLayout(builtin_, merged(user_), o);
  }
  return *normal_;
}

const EffectiveResult& Customization::editView() const {
  if (!edit_) edit_ = effectiveLayout(builtin_, merged(user_), editOptions());
  return *edit_;
}

namespace {

const Node* search(const std::vector<Node>& nodes, const std::string& id) {
  for (const Node& n : nodes) {
    if (n.id == id) return &n;
    if (const Node* inner = search(n.children, id)) return inner;
  }
  return nullptr;
}

bool pathTo(const std::vector<Node>& nodes, const std::string& id, std::vector<const Node*>& path) {
  for (const Node& n : nodes) {
    path.push_back(&n);
    if (n.id == id) return true;
    if (pathTo(n.children, id, path)) return true;
    path.pop_back();
  }
  return false;
}

void collectHidden(const std::vector<Node>& nodes, std::vector<const Node*>& out) {
  for (const Node& n : nodes) {
    if (!n.visible) out.push_back(&n);
    collectHidden(n.children, out);
  }
}

}  // namespace

const Node* Customization::find(const std::string& id) const {
  const LayoutSet& set = editView().layout;
  if (const Node* n = search(set.menuBar.menus, id)) return n;
  for (const ToolbarLayout& t : set.toolbars) {
    if (const Node* n = search(t.items, id)) return n;
  }
  for (const FreeFormPanelLayout& p : set.panels) {
    if (const Node* n = search(p.buttons, id)) return n;
  }
  return nullptr;
}

std::string Customization::shownLabel(const Node& node) const {
  switch (node.kind) {
    case Kind::Command:
    case Kind::FreeButton:
      if (!node.userLabel.empty()) return node.userLabel;
      return labeler_ ? labeler_(node.commandId) : node.commandId;
    case Kind::Separator: return "Separator";
    case Kind::Spacer: return "Spacer";
    case Kind::Group: return node.userLabel.empty() ? "Group" : node.userLabel;
    case Kind::Section: return node.shownLabel().empty() ? "Section" : node.shownLabel();
    default: return node.shownLabel();
  }
}

std::string Customization::pathOf(const std::string& id) const {
  const LayoutSet& set = editView().layout;
  std::vector<const Node*> path;
  std::string prefix;
  if (!pathTo(set.menuBar.menus, id, path)) {
    path.clear();
    for (const ToolbarLayout& t : set.toolbars) {
      if (pathTo(t.items, id, path)) {
        prefix = t.title.empty() ? t.id : t.title;
        break;
      }
      path.clear();
    }
  }
  if (path.empty()) {
    for (const FreeFormPanelLayout& p : set.panels) {
      if (pathTo(p.buttons, id, path)) {
        prefix = p.title.empty() ? p.id : p.title;
        break;
      }
      path.clear();
    }
  }
  std::string out = prefix;
  for (const Node* n : path) {
    if (n->kind == Kind::Section && n->shownLabel().empty()) continue;
    if (!out.empty()) out += " > ";
    out += shownLabel(*n);
  }
  return out;
}

std::vector<RestoreItem> Customization::restoreList() const {
  const LayoutSet& set = editView().layout;
  std::vector<const Node*> hidden;
  collectHidden(set.menuBar.menus, hidden);
  for (const ToolbarLayout& t : set.toolbars) collectHidden(t.items, hidden);
  for (const FreeFormPanelLayout& p : set.panels) collectHidden(p.buttons, hidden);
  std::vector<RestoreItem> out;
  out.reserve(hidden.size());
  for (const Node* n : hidden) {
    if (n->locked) continue;
    out.push_back({n->id, shownLabel(*n), pathOf(n->id), n->kind});
  }
  return out;
}

bool Customization::isLocked(const std::string& id) const {
  const Node* n = find(id);
  if (n != nullptr) return n->locked;
  const LayoutSet& set = editView().layout;
  if (id == set.menuBar.id) return set.menuBar.locked;
  if (const ToolbarLayout* t = findToolbar(set, id)) return t->locked;
  if (const FreeFormPanelLayout* p = findPanel(set, id)) return p->locked;
  return false;
}

std::string Customization::lockReason(const std::string& id) const {
  if (!isLocked(id)) return {};
  const LayoutSet& set = editView().layout;
  std::vector<const Node*> path;
  if (pathTo(set.menuBar.menus, id, path)) {
    for (const Node* n : path) {
      if (n->locked) return "\"" + shownLabel(*n) + "\" is locked by the application and cannot be changed.";
    }
  }
  return "This part of the interface is locked by the application and cannot be changed.";
}

// ---- edit session (decision D6) -----------------------------------------------------------------

void Customization::beginEditSession() { session_ = user_; }

bool Customization::revertSession() {
  if (!session_) return false;
  if (!(*session_ == user_)) {
    user_ = *session_;
    changed();
  }
  return true;
}

void Customization::commitSession() {
  if (!session_) return;
  session_.reset();
  if (onCommit_) onCommit_();
}

}  // namespace r1ui::commands::customize
