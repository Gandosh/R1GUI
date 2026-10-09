// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the data side of TreeView: the read-only TreeModel interface (ordered children per node,
//   labels, icons, per-node flags, a revision counter) and SimpleTreeModel, an in-memory model
//   (add, remove, rename, move, set flags) used by applications without their own scene graph, by
//   the gallery and by the tests (including 100k-row trees).
// Why: a layer tree over 100k nodes must not create a widget or copy a string per node; the view
//   asks the model for exactly the rows it draws. Nodes are identified by application-chosen
//   64-bit ids so selection, expansion and scroll targets survive refreshes, sorts and filters.
// Callers: TreeView (reads), application code and SimpleTreeModel users (write).
// Contract of TreeModel: kTreeRoot (0) is the invisible root; real ids are non-zero. childAt(parent,
//   i) is valid for i < childCount(parent). label() and icon() views stay valid until the next call
//   that changes the model. revision() changes whenever anything the view shows changes (structure,
//   labels, flags); the view compares it on every frame and refreshes itself when it moved.
// Invariants of SimpleTreeModel: ids are unique and non-zero, the structure is always a forest under
//   the root (a move that would create a cycle is rejected), and failed calls leave the model
//   unchanged. Labels are kept as given (UTF-8 is not validated: the text engine handles invalid
//   sequences when drawing), limited to kMaxLabelBytes.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace r1ui::widgets {

using NodeId = uint64_t;
inline constexpr NodeId kTreeRoot = 0;

struct NodeFlags {
  bool hidden = false;       // drawn at 50% opacity with a persistent "show" action
  bool locked = false;       // persistent "unlock" action
  bool component = false;    // icon in the `component` colour
  bool renamable = true;
  bool selectable = true;    // keyboard navigation skips rows that are not selectable
  bool draggable = true;
  bool acceptsDrops = true;
  bool hasActions = true;    // hover actions (visibility, lock) are offered
  friend bool operator==(const NodeFlags&, const NodeFlags&) = default;
};

class TreeModel {
 public:
  virtual ~TreeModel() = default;
  virtual size_t childCount(NodeId parent) const = 0;
  virtual NodeId childAt(NodeId parent, size_t index) const = 0;
  virtual bool contains(NodeId id) const = 0;
  virtual std::string_view label(NodeId id) const = 0;
  virtual std::string_view icon(NodeId) const { return {}; }
  virtual NodeFlags flags(NodeId) const { return {}; }
  virtual uint64_t revision() const = 0;
};

class SimpleTreeModel : public TreeModel {
 public:
  static constexpr size_t kMaxLabelBytes = 4096;
  static constexpr size_t kMaxNodes = 20'000'000;

  // ---- TreeModel ----
  size_t childCount(NodeId parent) const override;
  NodeId childAt(NodeId parent, size_t index) const override;
  bool contains(NodeId id) const override { return id != kTreeRoot && nodes_.count(id) != 0; }
  std::string_view label(NodeId id) const override;
  std::string_view icon(NodeId id) const override;
  NodeFlags flags(NodeId id) const override;
  uint64_t revision() const override { return revision_; }

  // ---- editing (each returns false and changes nothing when the request is invalid) ----
  // Adds `id` under `parent` (kTreeRoot allowed) at `index` (past the end = append).
  bool add(NodeId parent, NodeId id, std::string label, std::string icon = {}, NodeFlags flags = {}, size_t index = static_cast<size_t>(-1));
  // Removes the node and its whole subtree.
  bool remove(NodeId id);
  bool setLabel(NodeId id, std::string label);
  bool setIcon(NodeId id, std::string icon);
  bool setFlags(NodeId id, NodeFlags flags);
  // Moves the node (with its subtree) under `parent` at `index` (indices count children after the
  // node was taken out of its old place). Rejects the root, unknown ids and moves into itself.
  bool move(NodeId id, NodeId parent, size_t index);
  NodeId parentOf(NodeId id) const;
  size_t indexInParent(NodeId id) const;
  size_t size() const { return nodes_.size(); }
  void reserve(size_t nodes) { nodes_.reserve(nodes); }

 private:
  struct Node {
    NodeId parent = kTreeRoot;
    std::string label;
    std::string icon;
    NodeFlags flags;
    std::vector<NodeId> children;
  };
  const Node* find(NodeId id) const;
  std::vector<NodeId>& childrenOf(NodeId parent);
  const std::vector<NodeId>* childrenOfConst(NodeId parent) const;

  std::unordered_map<NodeId, Node> nodes_;
  std::vector<NodeId> rootChildren_;
  uint64_t revision_ = 1;
};

}  // namespace r1ui::widgets
