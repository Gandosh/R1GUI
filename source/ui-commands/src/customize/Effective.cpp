// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: effectiveLayout (Delta.h): builds a pointer tree from the built-in layouts, applies the
//   delta step by step with per-step validation, and converts the result back to plain values.
// Invariants: every step that cannot be applied is skipped and reported, the tree is legal after
//   every step (the parent/child rules of canContain, one container kind per move, no cycles, depth
//   and node limits, locked subtrees untouched), so the returned set always passes validateLayout.
//   The only recursion is over the tree and the tree depth is bounded by kMaxDepth, so no input can
//   overflow the stack. Cost is linear in nodes plus operations (anchors are looked up through the
//   id index and the parent's child list).
// Callers: Customization, tests. Calls: Layout.h.
#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_map>

#include "r1ui/commands/customize/Delta.h"

namespace r1ui::commands::customize {

const char* sideName(Side side) {
  switch (side) {
    case Side::End: return "end";
    case Side::Start: return "start";
    case Side::Before: return "before";
    case Side::After: return "after";
  }
  return "end";
}

bool parseSide(std::string_view name, Side& side) {
  for (const Side s : {Side::End, Side::Start, Side::Before, Side::After}) {
    if (name == sideName(s)) {
      side = s;
      return true;
    }
  }
  return false;
}

const char* reportCodeName(ReportEntry::Code code) {
  switch (code) {
    case ReportEntry::Code::MissingCommand: return "missing-command";
    case ReportEntry::Code::MissingNode: return "missing-node";
    case ReportEntry::Code::MissingParent: return "missing-parent";
    case ReportEntry::Code::MissingAnchor: return "missing-anchor";
    case ReportEntry::Code::IllegalParent: return "illegal-parent";
    case ReportEntry::Code::Cycle: return "cycle";
    case ReportEntry::Code::Locked: return "locked";
    case ReportEntry::Code::DuplicateId: return "duplicate-id";
    case ReportEntry::Code::Invalid: return "invalid";
    case ReportEntry::Code::Limit: return "limit";
  }
  return "?";
}

size_t Report::count(ReportEntry::Code code) const {
  return static_cast<size_t>(std::count_if(entries.begin(), entries.end(), [code](const ReportEntry& e) { return e.code == code; }));
}

bool Report::has(ReportEntry::Code code, std::string_view id) const {
  return std::any_of(entries.begin(), entries.end(), [&](const ReportEntry& e) { return e.code == code && e.id == id; });
}

namespace {

using Code = ReportEntry::Code;

// Cuts at kMaxLabelBytes on a UTF-8 boundary (the text was validated where it entered).
std::string clampText(const std::string& text) {
  if (text.size() <= kMaxLabelBytes) return text;
  size_t cut = kMaxLabelBytes;
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
  return text.substr(0, cut);
}

bool validRect(const Rect& r) {
  return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h) && r.w >= kMinButtonSize && r.h >= kMinButtonSize &&
         std::fabs(r.x) <= kMaxPanelSize && std::fabs(r.y) <= kMaxPanelSize && r.w <= kMaxPanelSize && r.h <= kMaxPanelSize;
}

// One node of the working tree. Containers (menu bar, toolbars, panels) are TNodes with `container`.
struct TNode {
  Node data;  // children stay empty; the tree is in `kids`
  Kind kind = Kind::Command;
  TNode* parent = nullptr;
  std::vector<TNode*> kids;
  bool container = false;
  bool locked = false;  // own flag or an ancestor's
  size_t depth = 0;     // containers 0, their children 1
  size_t index = 0;     // containers: position in the output vector of their kind
};

class Engine {
 public:
  Engine(const LayoutSet& builtin, const Delta& delta, const EffectiveOptions& options) : builtin_(builtin), delta_(delta), options_(options) {}

  EffectiveResult run() {
    buildContainers();
    applyAdded();
    applyMoves();
    applyEdits();
    applySettings();
    resolveMissing();
    return finish();
  }

 private:
  void report(Code code, const std::string& id, std::string detail) {
    if (report_.entries.size() < kMaxNodes) report_.entries.push_back({code, id, std::move(detail)});
  }

  TNode* find(const std::string& id) const {
    const auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : it->second;
  }

  // ---- building ------------------------------------------------------------------------------

  TNode* newTNode(Kind kind) {
    arena_.emplace_back();
    TNode* t = &arena_.back();
    t->kind = kind;
    return t;
  }

  TNode* container(Kind kind, const std::string& id, bool locked, size_t index) {
    if (!isValidNodeId(id)) {
      report(Code::Invalid, id.substr(0, 40), "container id is not valid");
      return nullptr;
    }
    if (byId_.count(id) != 0) {
      report(Code::DuplicateId, id, "container id already in use");
      return nullptr;
    }
    TNode* t = newTNode(kind);
    t->container = true;
    t->data.id = id;
    t->locked = locked;
    t->index = index;
    byId_[id] = t;
    return t;
  }

  // Adds `src` (and, when `deep`, its children) under `parent`. Returns the new node or nullptr.
  TNode* attach(TNode* parent, const Node& src, bool deep, const Placement* where) {
    if (!isValidNodeId(src.id)) {
      report(Code::Invalid, src.id.substr(0, 40), "node id is not valid");
      return nullptr;
    }
    if (byId_.count(src.id) != 0) {
      report(Code::DuplicateId, src.id, "id already in use");
      return nullptr;
    }
    if (!canContain(parent->kind, src.kind)) {
      report(Code::IllegalParent, src.id, std::string(kindName(src.kind)) + " cannot be placed in a " + kindName(parent->kind));
      return nullptr;
    }
    if (parent->depth + 1 > kMaxDepth) {
      report(Code::Limit, src.id, "nesting limit reached");
      return nullptr;
    }
    if (count_ >= kMaxNodes) {
      report(Code::Limit, src.id, "node limit reached");
      return nullptr;
    }
    ++count_;
    TNode* t = newTNode(src.kind);
    t->data = src;
    t->data.children.clear();
    t->data.label = clampText(src.label);
    t->data.userLabel = clampText(src.userLabel);
    t->data.missing = false;
    if (src.kind == Kind::FreeButton && !validRect(src.rect)) {
      report(Code::Invalid, src.id, "rectangle is not valid");
      t->data.rect = {0.0, 0.0, 32.0, 32.0};
    }
    t->locked = src.locked || parent->locked;
    byId_[src.id] = t;
    if (where != nullptr) {
      insertAt(parent, t, *where);
    } else {
      t->parent = parent;
      t->depth = parent->depth + 1;
      parent->kids.push_back(t);
    }
    if (deep) {
      for (const Node& child : src.children) attach(t, child, true, nullptr);
    }
    return t;
  }

  void buildContainers() {
    if (TNode* bar = container(Kind::MenuBar, builtin_.menuBar.id, builtin_.menuBar.locked, 0)) {
      menuBar_ = bar;
      for (const Node& menu : builtin_.menuBar.menus) attach(bar, menu, true, nullptr);
    }
    for (size_t i = 0; i < builtin_.toolbars.size() && toolbars_.size() < kMaxContainers; ++i) {
      const ToolbarLayout& layout = builtin_.toolbars[i];
      if (TNode* t = container(Kind::Toolbar, layout.id, layout.locked, i)) {
        toolbars_.push_back({&layout, t});
        for (const Node& item : layout.items) attach(t, item, true, nullptr);
      }
    }
    for (size_t i = 0; i < builtin_.panels.size() && panels_.size() < kMaxContainers; ++i) {
      const FreeFormPanelLayout& layout = builtin_.panels[i];
      if (TNode* t = container(Kind::Panel, layout.id, layout.locked, i)) {
        panels_.push_back({&layout, t});
        for (const Node& button : layout.buttons) attach(t, button, true, nullptr);
      }
    }
    for (const ToolbarLayout& layout : delta_.userToolbars) {
      if (toolbars_.size() >= kMaxContainers) {
        report(Code::Limit, layout.id, "container limit reached");
        break;
      }
      if (TNode* t = container(Kind::Toolbar, layout.id, false, builtin_.toolbars.size() + userToolbars_)) {
        ++userToolbars_;
        toolbars_.push_back({&layout, t});
      }
    }
    for (const FreeFormPanelLayout& layout : delta_.userPanels) {
      if (panels_.size() >= kMaxContainers) {
        report(Code::Limit, layout.id, "container limit reached");
        break;
      }
      if (TNode* t = container(Kind::Panel, layout.id, false, builtin_.panels.size() + userPanels_)) {
        ++userPanels_;
        panels_.push_back({&layout, t});
      }
    }
  }

  // ---- placement -----------------------------------------------------------------------------

  static void setDepth(TNode* t, size_t depth) {
    t->depth = depth;
    for (TNode* kid : t->kids) setDepth(kid, depth + 1);
  }

  static size_t height(const TNode* t) {
    size_t h = 0;
    for (const TNode* kid : t->kids) h = std::max(h, height(kid));
    return h + 1;
  }

  // Links `t` into `parent` at `where`; the anchor falls back to the end of the parent.
  void insertAt(TNode* parent, TNode* t, const Placement& where) {
    t->parent = parent;
    auto& kids = parent->kids;
    auto position = kids.end();
    if (where.side == Side::Start) {
      position = kids.begin();
    } else if (where.side == Side::Before || where.side == Side::After) {
      const auto anchor = std::find_if(kids.begin(), kids.end(), [&](const TNode* k) { return k->data.id == where.anchor && k != t; });
      if (anchor == kids.end()) {
        report(Code::MissingAnchor, t->data.id, "anchor '" + where.anchor + "' is not in '" + parent->data.id + "'; placed last");
      } else {
        position = where.side == Side::Before ? anchor : anchor + 1;
      }
    }
    kids.insert(position, t);
    setDepth(t, parent->depth + 1);
  }

  static TNode* rootContainer(TNode* t) {
    while (t->parent != nullptr) t = t->parent;
    return t;
  }

  static bool within(const TNode* node, const TNode* ancestor) {
    for (; node != nullptr; node = node->parent) {
      if (node == ancestor) return true;
    }
    return false;
  }

  void applyAdded() {
    for (const AddedNode& added : delta_.added) {
      TNode* parent = find(added.at.parent);
      if (parent == nullptr) {
        report(Code::MissingParent, added.node.id, "parent '" + added.at.parent + "' does not exist");
        continue;
      }
      if (parent->locked) {
        report(Code::Locked, added.node.id, "parent '" + parent->data.id + "' is locked");
        continue;
      }
      Node src = added.node;
      src.user = true;
      src.locked = false;
      attach(parent, src, false, &added.at);
    }
  }

  void applyMoves() {
    for (const MoveEdit& move : delta_.moves) {
      TNode* node = find(move.node);
      if (node == nullptr || node->container) {
        report(node == nullptr ? Code::MissingNode : Code::IllegalParent, move.node, "cannot move");
        continue;
      }
      TNode* target = find(move.to.parent);
      if (target == nullptr) {
        report(Code::MissingParent, move.node, "parent '" + move.to.parent + "' does not exist");
        continue;
      }
      if (node->locked || target->locked) {
        report(Code::Locked, move.node, "a locked node or parent is involved");
        continue;
      }
      if (!canContain(target->kind, node->kind) || rootContainer(target)->kind != rootContainer(node)->kind) {
        report(Code::IllegalParent, move.node, std::string(kindName(node->kind)) + " cannot move into a " + kindName(target->kind));
        continue;
      }
      if (within(target, node)) {
        report(Code::Cycle, move.node, "target is inside the moved node");
        continue;
      }
      if (target->depth + height(node) > kMaxDepth) {
        report(Code::Limit, move.node, "nesting limit reached");
        continue;
      }
      auto& old = node->parent->kids;
      old.erase(std::find(old.begin(), old.end(), node));
      insertAt(target, node, move.to);
    }
  }

  // ---- edits ---------------------------------------------------------------------------------

  static bool takesLabel(Kind kind) {
    return kind == Kind::Menu || kind == Kind::Section || kind == Kind::Heading || kind == Kind::Submenu || kind == Kind::Command || kind == Kind::FreeButton;
  }

  void applyEdits() {
    for (const auto& [id, edit] : delta_.edits) {
      TNode* node = find(id);
      if (node == nullptr || node->container) {
        report(Code::MissingNode, id, "edit of a node that does not exist");
        continue;
      }
      if (node->locked) {
        report(Code::Locked, id, "node is locked");
        continue;
      }
      if (edit.hidden) node->data.visible = !*edit.hidden;
      if (edit.label) {
        if (takesLabel(node->kind)) {
          node->data.userLabel = clampText(*edit.label);
        } else {
          report(Code::IllegalParent, id, "this kind of node has no label");
        }
      }
      if (edit.rect) {
        if (node->kind != Kind::FreeButton) {
          report(Code::IllegalParent, id, "only free-form buttons have a rectangle");
        } else if (!validRect(*edit.rect)) {
          report(Code::Invalid, id, "rectangle is not valid");
        } else {
          node->data.rect = *edit.rect;
        }
      }
    }
  }

  void applySettings() {
    for (const auto& [id, edit] : delta_.toolbarEdits) {
      const auto it = std::find_if(toolbars_.begin(), toolbars_.end(), [&](const ToolbarSlot& s) { return s.layout->id == id; });
      if (it == toolbars_.end()) {
        report(Code::MissingNode, id, "settings of a toolbar that does not exist");
        continue;
      }
      if (it->node->locked) {
        report(Code::Locked, id, "toolbar is locked");
        continue;
      }
      if (edit.sizeStep) it->sizeStep = *edit.sizeStep;
      if (edit.gap) {
        if (std::isfinite(*edit.gap) && *edit.gap >= kMinToolbarGap && *edit.gap <= kMaxToolbarGap) {
          it->gap = *edit.gap;
        } else {
          report(Code::Invalid, id, "gap is out of range");
        }
      }
    }
    for (const auto& [id, edit] : delta_.panelEdits) {
      const auto it = std::find_if(panels_.begin(), panels_.end(), [&](const PanelSlot& s) { return s.layout->id == id; });
      if (it == panels_.end()) {
        report(Code::MissingNode, id, "settings of a panel that does not exist");
        continue;
      }
      if (it->node->locked) {
        report(Code::Locked, id, "panel is locked");
        continue;
      }
      if (edit.snap) it->snap = *edit.snap;
      if (edit.grid) {
        if (std::isfinite(*edit.grid) && *edit.grid >= 1.0 && *edit.grid <= 256.0) {
          it->grid = *edit.grid;
        } else {
          report(Code::Invalid, id, "grid is out of range");
        }
      }
    }
  }

  // ---- missing commands, hidden nodes --------------------------------------------------------

  // Marks (and unless kept, removes) command references whose command is gone and hidden nodes, then
  // groups left without commands.
  void prune(TNode* t) {
    for (size_t i = 0; i < t->kids.size();) {
      TNode* kid = t->kids[i];
      bool drop = false;
      if ((kid->kind == Kind::Command || kid->kind == Kind::FreeButton) && options_.exists && !options_.exists(kid->data.commandId)) {
        kid->data.missing = true;
        report(Code::MissingCommand, kid->data.id, "command '" + kid->data.commandId + "' is not registered");
        drop = !options_.keepMissing;
      }
      if (!drop && !options_.keepHidden && !kid->data.visible) drop = true;
      if (!drop && !kid->kids.empty()) prune(kid);
      if (!drop && kid->kind == Kind::Group && kid->kids.empty() && !(options_.keepHidden && options_.keepMissing)) drop = true;
      if (drop) {
        t->kids.erase(t->kids.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        ++i;
      }
    }
  }

  void resolveMissing() {
    if (menuBar_ != nullptr) prune(menuBar_);
    for (const ToolbarSlot& s : toolbars_) prune(s.node);
    for (const PanelSlot& s : panels_) prune(s.node);
  }

  // ---- output --------------------------------------------------------------------------------

  Node convert(const TNode* t) const {
    Node n = t->data;
    n.locked = t->locked;
    n.children.reserve(t->kids.size());
    for (const TNode* kid : t->kids) n.children.push_back(convert(kid));
    return n;
  }

  std::vector<Node> convertKids(const TNode* t) const {
    std::vector<Node> out;
    out.reserve(t->kids.size());
    for (const TNode* kid : t->kids) out.push_back(convert(kid));
    return out;
  }

  EffectiveResult finish() {
    EffectiveResult result;
    result.layout.menuBar.id = builtin_.menuBar.id;
    result.layout.menuBar.locked = builtin_.menuBar.locked;
    if (menuBar_ != nullptr) result.layout.menuBar.menus = convertKids(menuBar_);
    for (const ToolbarSlot& s : toolbars_) {
      ToolbarLayout out = *s.layout;
      out.items = convertKids(s.node);
      out.locked = s.node->locked;
      out.sizeStep = s.sizeStep.value_or(s.layout->sizeStep);
      out.gap = s.gap.value_or(s.layout->gap);
      result.layout.toolbars.push_back(std::move(out));
    }
    for (const PanelSlot& s : panels_) {
      FreeFormPanelLayout out = *s.layout;
      out.buttons = convertKids(s.node);
      out.locked = s.node->locked;
      out.snap = s.snap.value_or(s.layout->snap);
      out.grid = s.grid.value_or(s.layout->grid);
      result.layout.panels.push_back(std::move(out));
    }
    result.report = std::move(report_);
    return result;
  }

  struct ToolbarSlot {
    const ToolbarLayout* layout;
    TNode* node;
    std::optional<SizeStep> sizeStep;
    std::optional<double> gap;
  };
  struct PanelSlot {
    const FreeFormPanelLayout* layout;
    TNode* node;
    std::optional<bool> snap;
    std::optional<double> grid;
  };

  const LayoutSet& builtin_;
  const Delta& delta_;
  const EffectiveOptions& options_;
  std::deque<TNode> arena_;
  std::unordered_map<std::string, TNode*> byId_;
  TNode* menuBar_ = nullptr;
  std::vector<ToolbarSlot> toolbars_;
  std::vector<PanelSlot> panels_;
  size_t userToolbars_ = 0;
  size_t userPanels_ = 0;
  size_t count_ = 0;
  Report report_;
};

}  // namespace

EffectiveResult effectiveLayout(const LayoutSet& builtin, const Delta& delta, const EffectiveOptions& options) {
  return Engine(builtin, delta, options).run();
}

}  // namespace r1ui::commands::customize
