// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of SimpleTreeModel (TreeModel.h).
// Invariants: nodes_ and the children vectors are kept consistent by every mutator (each parent link
//   matches the children list that contains the id); revision_ is bumped exactly when something the
//   view shows changed.
// Callers: TreeView, tests, the gallery.
#include "r1ui/widgets/tree/TreeModel.h"

#include <algorithm>

namespace r1ui::widgets {

const SimpleTreeModel::Node* SimpleTreeModel::find(NodeId id) const {
  const auto it = nodes_.find(id);
  return it == nodes_.end() ? nullptr : &it->second;
}

const std::vector<NodeId>* SimpleTreeModel::childrenOfConst(NodeId parent) const {
  if (parent == kTreeRoot) return &rootChildren_;
  const Node* n = find(parent);
  return n != nullptr ? &n->children : nullptr;
}

std::vector<NodeId>& SimpleTreeModel::childrenOf(NodeId parent) { return parent == kTreeRoot ? rootChildren_ : nodes_.at(parent).children; }

size_t SimpleTreeModel::childCount(NodeId parent) const {
  const std::vector<NodeId>* c = childrenOfConst(parent);
  return c != nullptr ? c->size() : 0;
}

NodeId SimpleTreeModel::childAt(NodeId parent, size_t index) const {
  const std::vector<NodeId>* c = childrenOfConst(parent);
  return c != nullptr && index < c->size() ? (*c)[index] : kTreeRoot;
}

std::string_view SimpleTreeModel::label(NodeId id) const {
  const Node* n = find(id);
  return n != nullptr ? std::string_view(n->label) : std::string_view();
}

std::string_view SimpleTreeModel::icon(NodeId id) const {
  const Node* n = find(id);
  return n != nullptr ? std::string_view(n->icon) : std::string_view();
}

NodeFlags SimpleTreeModel::flags(NodeId id) const {
  const Node* n = find(id);
  return n != nullptr ? n->flags : NodeFlags{};
}

NodeId SimpleTreeModel::parentOf(NodeId id) const {
  const Node* n = find(id);
  return n != nullptr ? n->parent : kTreeRoot;
}

size_t SimpleTreeModel::indexInParent(NodeId id) const {
  const Node* n = find(id);
  if (n == nullptr) return static_cast<size_t>(-1);
  const std::vector<NodeId>* siblings = childrenOfConst(n->parent);
  const auto it = std::find(siblings->begin(), siblings->end(), id);
  return static_cast<size_t>(it - siblings->begin());
}

bool SimpleTreeModel::add(NodeId parent, NodeId id, std::string label, std::string icon, NodeFlags flags, size_t index) {
  if (id == kTreeRoot || nodes_.count(id) != 0 || nodes_.size() >= kMaxNodes) return false;
  if (parent != kTreeRoot && find(parent) == nullptr) return false;
  if (label.size() > kMaxLabelBytes) label.resize(kMaxLabelBytes);
  Node n;
  n.parent = parent;
  n.label = std::move(label);
  n.icon = std::move(icon);
  n.flags = flags;
  nodes_.emplace(id, std::move(n));
  std::vector<NodeId>& siblings = childrenOf(parent);
  const size_t at = std::min(index, siblings.size());
  siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(at), id);
  ++revision_;
  return true;
}

bool SimpleTreeModel::remove(NodeId id) {
  const Node* n = find(id);
  if (n == nullptr) return false;
  std::vector<NodeId>& siblings = childrenOf(n->parent);
  siblings.erase(std::find(siblings.begin(), siblings.end(), id));
  std::vector<NodeId> stack{id};
  while (!stack.empty()) {
    const NodeId cur = stack.back();
    stack.pop_back();
    const auto it = nodes_.find(cur);
    if (it == nodes_.end()) continue;
    stack.insert(stack.end(), it->second.children.begin(), it->second.children.end());
    nodes_.erase(it);
  }
  ++revision_;
  return true;
}

bool SimpleTreeModel::setLabel(NodeId id, std::string label) {
  const auto it = nodes_.find(id);
  if (it == nodes_.end()) return false;
  if (label.size() > kMaxLabelBytes) label.resize(kMaxLabelBytes);
  if (it->second.label == label) return true;
  it->second.label = std::move(label);
  ++revision_;
  return true;
}

bool SimpleTreeModel::setIcon(NodeId id, std::string icon) {
  const auto it = nodes_.find(id);
  if (it == nodes_.end()) return false;
  if (it->second.icon == icon) return true;
  it->second.icon = std::move(icon);
  ++revision_;
  return true;
}

bool SimpleTreeModel::setFlags(NodeId id, NodeFlags flags) {
  const auto it = nodes_.find(id);
  if (it == nodes_.end()) return false;
  if (it->second.flags == flags) return true;
  it->second.flags = flags;
  ++revision_;
  return true;
}

bool SimpleTreeModel::move(NodeId id, NodeId parent, size_t index) {
  const Node* n = find(id);
  if (n == nullptr || (parent != kTreeRoot && find(parent) == nullptr)) return false;
  // The new parent must not be the node or one of its descendants.
  for (NodeId p = parent; p != kTreeRoot; p = find(p)->parent) {
    if (p == id) return false;
  }
  std::vector<NodeId>& old = childrenOf(n->parent);
  old.erase(std::find(old.begin(), old.end(), id));
  nodes_.at(id).parent = parent;
  std::vector<NodeId>& fresh = childrenOf(parent);
  fresh.insert(fresh.begin() + static_cast<std::ptrdiff_t>(std::min(index, fresh.size())), id);
  ++revision_;
  return true;
}

}  // namespace r1ui::widgets
