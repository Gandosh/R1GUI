// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: WidgetTree storage management, linking and the validation that makes every mutation
//   all-or-nothing.
// Why: see WidgetTree.h. All checks run before any link is touched, so a refused operation leaves
//   the tree exactly as it was.
// Callers: WidgetTree.h consumers. Calls: nothing outside the standard library.
#include "r1ui/core/tree/WidgetTree.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace r1ui::core::tree {

const char* describe(TreeError error) {
  switch (error) {
    case TreeError::None: return "ok";
    case TreeError::StaleId: return "stale widget id";
    case TreeError::NodeLimit: return "widget node limit reached";
    case TreeError::DepthLimit: return "widget depth limit reached";
    case TreeError::WouldCreateCycle: return "reparent would create a cycle";
    case TreeError::IsRoot: return "a root widget cannot be moved";
    case TreeError::NotChildOfParent: return "sibling is not a child of the target parent";
    case TreeError::Busy: return "tree is locked against mutation";
  }
  return "unknown tree error";
}

WidgetTree::WidgetTree(TreeLimits limits) : limits_(limits) {
  if (limits.maxNodes == 0) throw std::invalid_argument("TreeLimits.maxNodes must be > 0");
  if (limits.maxDepth > kMaxSupportedDepth) {
    throw std::invalid_argument("TreeLimits.maxDepth exceeds kMaxSupportedDepth");
  }
}

// ---- lookup ----

const WidgetTree::Slot* WidgetTree::slotFor(WidgetId id) const {
  if (id.index >= slots_.size()) return nullptr;
  const Slot& slot = slots_[id.index];
  return (slot.alive && slot.generation == id.generation) ? &slot : nullptr;
}

WidgetTree::Slot* WidgetTree::slotFor(WidgetId id) {
  return const_cast<Slot*>(static_cast<const WidgetTree*>(this)->slotFor(id));
}

WidgetId WidgetTree::idOf(uint32_t index) const {
  if (index == kNone) return kNoWidget;
  return WidgetId{index, slots_[index].generation};
}

Widget* WidgetTree::get(WidgetId id) {
  Slot* slot = slotFor(id);
  return slot ? &slot->widget : nullptr;
}

const Widget* WidgetTree::get(WidgetId id) const {
  const Slot* slot = slotFor(id);
  return slot ? &slot->widget : nullptr;
}

WidgetId WidgetTree::parent(WidgetId id) const {
  const Slot* s = slotFor(id);
  return s ? idOf(s->widget.parent_) : kNoWidget;
}
WidgetId WidgetTree::firstChild(WidgetId id) const {
  const Slot* s = slotFor(id);
  return s ? idOf(s->widget.firstChild_) : kNoWidget;
}
WidgetId WidgetTree::lastChild(WidgetId id) const {
  const Slot* s = slotFor(id);
  return s ? idOf(s->widget.lastChild_) : kNoWidget;
}
WidgetId WidgetTree::nextSibling(WidgetId id) const {
  const Slot* s = slotFor(id);
  return s ? idOf(s->widget.nextSibling_) : kNoWidget;
}
WidgetId WidgetTree::prevSibling(WidgetId id) const {
  const Slot* s = slotFor(id);
  return s ? idOf(s->widget.prevSibling_) : kNoWidget;
}
uint32_t WidgetTree::childCount(WidgetId id) const {
  const Slot* s = slotFor(id);
  return s ? s->widget.childCount_ : 0;
}
uint32_t WidgetTree::depth(WidgetId id) const {
  const Slot* s = slotFor(id);
  return s ? s->widget.depth_ : 0;
}

bool WidgetTree::isAncestor(WidgetId ancestor, WidgetId node) const {
  const Slot* a = slotFor(ancestor);
  const Slot* n = slotFor(node);
  if (a == nullptr || n == nullptr) return false;
  const uint32_t target = ancestor.index;
  for (uint32_t cur = n->widget.parent_; cur != kNone; cur = slots_[cur].widget.parent_) {
    if (cur == target) return true;
  }
  return false;
}

void WidgetTree::ancestorsOf(WidgetId id, std::vector<WidgetId>& out) const {
  const Slot* s = slotFor(id);
  if (s == nullptr) return;
  out.push_back(id);
  for (uint32_t cur = s->widget.parent_; cur != kNone; cur = slots_[cur].widget.parent_) {
    out.push_back(idOf(cur));
  }
}

void WidgetTree::collectPreorder(WidgetId root, bool includeSelf, std::vector<WidgetId>& out) const {
  const Slot* r = slotFor(root);
  if (r == nullptr) return;
  if (includeSelf) out.push_back(root);
  // Iterative pre-order: push children in reverse so the first child is popped first.
  std::vector<uint32_t> stack;
  for (uint32_t c = r->widget.lastChild_; c != kNone; c = slots_[c].widget.prevSibling_) {
    stack.push_back(c);
  }
  while (!stack.empty()) {
    const uint32_t index = stack.back();
    stack.pop_back();
    out.push_back(idOf(index));
    for (uint32_t c = slots_[index].widget.lastChild_; c != kNone; c = slots_[c].widget.prevSibling_) {
      stack.push_back(c);
    }
  }
}

// ---- linking ----

uint32_t WidgetTree::allocate() {
  uint32_t index;
  if (freeHead_ != kNone) {
    index = freeHead_;
    freeHead_ = slots_[index].nextFree;
  } else {
    index = static_cast<uint32_t>(slots_.size());
    slots_.emplace_back();
  }
  Slot& slot = slots_[index];
  slot.widget = Widget{};
  slot.alive = true;
  slot.nextFree = kNone;
  return index;
}

// Inserts `child` (currently unlinked) under `parent` before `before` (kNone = at the end).
void WidgetTree::link(uint32_t child, uint32_t parent, uint32_t before) {
  Widget& c = slots_[child].widget;
  Widget& p = slots_[parent].widget;
  c.parent_ = parent;
  c.depth_ = p.depth_ + 1;
  if (before == kNone) {
    c.prevSibling_ = p.lastChild_;
    c.nextSibling_ = kNone;
    if (p.lastChild_ != kNone) slots_[p.lastChild_].widget.nextSibling_ = child;
    else p.firstChild_ = child;
    p.lastChild_ = child;
  } else {
    Widget& b = slots_[before].widget;
    c.nextSibling_ = before;
    c.prevSibling_ = b.prevSibling_;
    if (b.prevSibling_ != kNone) slots_[b.prevSibling_].widget.nextSibling_ = child;
    else p.firstChild_ = child;
    b.prevSibling_ = child;
  }
  ++p.childCount_;
}

void WidgetTree::unlink(uint32_t child) {
  Widget& c = slots_[child].widget;
  if (c.parent_ == kNone) return;
  Widget& p = slots_[c.parent_].widget;
  if (c.prevSibling_ != kNone) slots_[c.prevSibling_].widget.nextSibling_ = c.nextSibling_;
  else p.firstChild_ = c.nextSibling_;
  if (c.nextSibling_ != kNone) slots_[c.nextSibling_].widget.prevSibling_ = c.prevSibling_;
  else p.lastChild_ = c.prevSibling_;
  --p.childCount_;
  c.parent_ = kNone;
  c.prevSibling_ = kNone;
  c.nextSibling_ = kNone;
}

// Height of the subtree below `index` (0 for a leaf), computed without recursion.
uint32_t WidgetTree::subtreeHeight(uint32_t index) const {
  std::vector<std::pair<uint32_t, uint32_t>> stack{{index, 0}};
  uint32_t height = 0;
  while (!stack.empty()) {
    const auto [node, level] = stack.back();
    stack.pop_back();
    if (level > height) height = level;
    for (uint32_t c = slots_[node].widget.firstChild_; c != kNone; c = slots_[c].widget.nextSibling_) {
      stack.emplace_back(c, level + 1);
    }
  }
  return height;
}

void WidgetTree::setDepths(uint32_t index, uint32_t depth) {
  std::vector<std::pair<uint32_t, uint32_t>> stack{{index, depth}};
  while (!stack.empty()) {
    const auto [node, d] = stack.back();
    stack.pop_back();
    slots_[node].widget.depth_ = d;
    for (uint32_t c = slots_[node].widget.firstChild_; c != kNone; c = slots_[c].widget.nextSibling_) {
      stack.emplace_back(c, d + 1);
    }
  }
}

TreeError WidgetTree::checkBefore(uint32_t parentIndex, WidgetId before, uint32_t& beforeIndex) const {
  beforeIndex = kNone;
  if (!before.valid()) return TreeError::None;
  const Slot* b = slotFor(before);
  if (b == nullptr) return TreeError::StaleId;
  if (b->widget.parent_ != parentIndex) return TreeError::NotChildOfParent;
  beforeIndex = before.index;
  return TreeError::None;
}

// ---- mutation ----

CreateResult WidgetTree::createRoot() {
  if (lockCount_ != 0) return {kNoWidget, TreeError::Busy};
  if (nodeCount_ >= limits_.maxNodes || (freeHead_ == kNone && slots_.size() >= kNone)) {
    return {kNoWidget, TreeError::NodeLimit};
  }
  const uint32_t index = allocate();
  slots_[index].widget.depth_ = 0;
  ++nodeCount_;
  ++structureVersion_;
  return {idOf(index), TreeError::None};
}

CreateResult WidgetTree::create(WidgetId parentId, WidgetId before) {
  if (lockCount_ != 0) return {kNoWidget, TreeError::Busy};
  const Slot* p = slotFor(parentId);
  if (p == nullptr) return {kNoWidget, TreeError::StaleId};
  if (nodeCount_ >= limits_.maxNodes || (freeHead_ == kNone && slots_.size() >= kNone)) {
    return {kNoWidget, TreeError::NodeLimit};
  }
  if (p->widget.depth_ + 1 > limits_.maxDepth) return {kNoWidget, TreeError::DepthLimit};
  uint32_t beforeIndex = kNone;
  if (const TreeError e = checkBefore(parentId.index, before, beforeIndex); e != TreeError::None) {
    return {kNoWidget, e};
  }
  const uint32_t index = allocate();
  link(index, parentId.index, beforeIndex);
  ++nodeCount_;
  ++structureVersion_;
  return {idOf(index), TreeError::None};
}

TreeError WidgetTree::destroy(WidgetId id) {
  if (lockCount_ != 0) return TreeError::Busy;
  if (slotFor(id) == nullptr) return TreeError::StaleId;
  unlink(id.index);

  // Collect the subtree first, then free: freeing while walking would clobber the links.
  std::vector<uint32_t> doomed{id.index};
  for (size_t i = 0; i < doomed.size(); ++i) {
    for (uint32_t c = slots_[doomed[i]].widget.firstChild_; c != kNone;
         c = slots_[c].widget.nextSibling_) {
      doomed.push_back(c);
    }
  }
  for (const uint32_t index : doomed) {
    Slot& slot = slots_[index];
    slot.widget = Widget{};  // releases the name string and clears every link
    slot.alive = false;
    if (slot.generation == std::numeric_limits<uint32_t>::max()) {
      slot.nextFree = kNone;  // generation exhausted: retire the slot, never reuse it
    } else {
      ++slot.generation;
      slot.nextFree = freeHead_;
      freeHead_ = index;
    }
  }
  nodeCount_ -= doomed.size();
  ++structureVersion_;
  return TreeError::None;
}

TreeError WidgetTree::reparent(WidgetId child, WidgetId newParent, WidgetId before) {
  if (lockCount_ != 0) return TreeError::Busy;
  const Slot* c = slotFor(child);
  const Slot* p = slotFor(newParent);
  if (c == nullptr || p == nullptr) return TreeError::StaleId;
  if (c->widget.parent_ == kNone) return TreeError::IsRoot;
  if (child.index == newParent.index || isAncestor(child, newParent)) {
    return TreeError::WouldCreateCycle;
  }
  uint32_t beforeIndex = kNone;
  if (const TreeError e = checkBefore(newParent.index, before, beforeIndex); e != TreeError::None) {
    return e;
  }
  if (beforeIndex == child.index) return TreeError::None;  // already in place
  const uint32_t newDepth = p->widget.depth_ + 1;
  if (newDepth + subtreeHeight(child.index) > limits_.maxDepth) return TreeError::DepthLimit;

  const uint32_t oldDepth = c->widget.depth_;
  unlink(child.index);
  link(child.index, newParent.index, beforeIndex);
  if (newDepth != oldDepth) setDepths(child.index, newDepth);
  ++structureVersion_;
  return TreeError::None;
}

TreeError WidgetTree::reorder(WidgetId child, WidgetId before) {
  const Slot* c = slotFor(child);
  if (c == nullptr) return TreeError::StaleId;
  if (c->widget.parent_ == kNone) return TreeError::IsRoot;
  return reparent(child, idOf(c->widget.parent_), before);
}

}  // namespace r1ui::core::tree
