// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: mergeDeltas (Delta.h), the layering of the per-workspace override over the per-user delta.
// Invariants: the result contains every entry of `base` and `over` with `over` winning field by
//   field; the order of moves and added nodes is base first, then over, with a replaced entry taking
//   the position of the replacement, so later moves can still anchor on earlier ones.
// Callers: Customization (effective views), CustomizationIo (import merge), tests.
#include <algorithm>

#include "r1ui/commands/customize/Delta.h"

namespace r1ui::commands::customize {

namespace {

template <class T, class IdOf>
void mergeById(std::vector<T>& into, const std::vector<T>& from, IdOf idOf) {
  for (const T& item : from) {
    const auto it = std::find_if(into.begin(), into.end(), [&](const T& existing) { return idOf(existing) == idOf(item); });
    if (it != into.end()) {
      *it = item;
    } else {
      into.push_back(item);
    }
  }
}

}  // namespace

Delta mergeDeltas(const Delta& base, const Delta& over) {
  Delta out = base;
  for (const auto& [id, edit] : over.edits) {
    NodeEdit& target = out.edits[id];
    if (edit.hidden) target.hidden = edit.hidden;
    if (edit.label) target.label = edit.label;
    if (edit.rect) target.rect = edit.rect;
  }
  for (const MoveEdit& move : over.moves) {
    out.moves.erase(std::remove_if(out.moves.begin(), out.moves.end(), [&](const MoveEdit& m) { return m.node == move.node; }), out.moves.end());
    out.moves.push_back(move);
  }
  mergeById(out.added, over.added, [](const AddedNode& a) -> const std::string& { return a.node.id; });
  mergeById(out.userToolbars, over.userToolbars, [](const ToolbarLayout& t) -> const std::string& { return t.id; });
  mergeById(out.userPanels, over.userPanels, [](const FreeFormPanelLayout& p) -> const std::string& { return p.id; });
  for (const auto& [id, edit] : over.toolbarEdits) {
    ToolbarEdit& target = out.toolbarEdits[id];
    if (edit.sizeStep) target.sizeStep = edit.sizeStep;
    if (edit.gap) target.gap = edit.gap;
  }
  for (const auto& [id, edit] : over.panelEdits) {
    PanelEdit& target = out.panelEdits[id];
    if (edit.snap) target.snap = edit.snap;
    if (edit.grid) target.grid = edit.grid;
  }
  out.serial = std::max(base.serial, over.serial);
  return out;
}

}  // namespace r1ui::commands::customize
