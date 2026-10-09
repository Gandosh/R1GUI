// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of MenuEditor.h, part 3: the drop logic (the DragTarget side): where the pointer
//   would put the dragged entry, section, menu or palette command, whether the model accepts it
//   (Customization::preview), the insertion indicator and the drop itself.
// Invariants: the indicator is shown only where the model accepts the change; a drop re-locates the
//   position at the release point and applies exactly what the indicator promised; nothing is stored
//   while hovering.
// Callers: DragHub (through DragTarget), tests.
#include <algorithm>

#include "CustomizeCommon.h"
#include "MenuEditorMetrics.h"
#include "r1ui/widgets/customize/MenuEditor.h"

namespace r1ui::widgets {

namespace cz = commands::customize;
namespace layout = core::layout;
using cust::kLabelLeft;

namespace {

// Runs the operation a drop stands for on `m` (a real call or a preview).
cz::EditResult runDrop(cz::Customization& m, const DragPayload& payload, const cz::Placement& at) {
  if (payload.kind == DragPayload::Kind::Command) return m.addCommand(at.parent, payload.commandId, at.anchor, at.side);
  return m.move(payload.nodeId, at);
}

}  // namespace

// ---- drag and drop ------------------------------------------------------------------------------

cz::Kind MenuEditor::payloadKind(const DragPayload& payload) const {
  if (payload.kind == DragPayload::Kind::Command) return cz::Kind::Command;
  const cz::Node* node = controller_.model().find(payload.nodeId);
  return node != nullptr ? node->kind : cz::Kind::Command;
}

MenuEditor::Candidate MenuEditor::locate(const DragPayload& payload, double x, double y) const {
  Candidate c;
  const layout::Rect self = ui().absRect(id());
  const double lx = x - self.x, ly = y - self.y;
  if (lx < 0.0 || ly < 0.0 || lx >= self.w || ly >= self.h) return c;
  const cz::Kind kind = payloadKind(payload);
  const cz::LayoutSet& set = controller_.model().editView().layout;
  const double lineLeft = 6.0, lineWidth = std::max(0.0, self.w - 12.0);

  if (ly < kTitleHeight) {
    for (size_t i = 0; i < titles_.size(); ++i) {
      const TitleTab& t = titles_[i];
      if (lx < t.x || lx >= t.x + t.w) continue;
      if (kind == cz::Kind::Menu) {
        const bool before = lx < t.x + t.w * 0.5;
        c.placement = {set.menuBar.id, t.id, before ? cz::Side::Before : cz::Side::After};
        c.line = local((before ? t.x : t.x + t.w) - 1.0, 4.0, 2.0, kTitleHeight - 8.0);
      } else if (kind == cz::Kind::Section) {
        c.placement = {t.id, "", cz::Side::End};
        c.title = static_cast<int>(i);
      } else {
        const cz::Node* menu = controller_.model().find(t.id);
        if (menu == nullptr) return c;
        if (!menu->children.empty()) {
          c.placement = {menu->children.back().id, "", cz::Side::End};
        } else {
          c.placement = {menu->id, "", cz::Side::End};
        }
        c.title = static_cast<int>(i);
      }
      c.valid = true;
      return c;
    }
    return c;
  }
  if (kind == cz::Kind::Menu) return c;
  if (current_.empty()) return c;

  // Top-level sections of the current menu, with the y range each one covers.
  struct Range {
    std::string id;
    double top, bottom;
  };
  std::vector<Range> sections;
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].kind == cz::Kind::Section && rows_[i].depth == 0) sections.push_back({rows_[i].id, rows_[i].y, rows_.back().y + rows_.back().h});
  }
  for (size_t i = 0; i + 1 < sections.size(); ++i) sections[i].bottom = sections[i + 1].top;
  const double endY = rows_.empty() ? rowsTop() : rows_.back().y + rows_.back().h;

  if (kind == cz::Kind::Section) {
    if (sections.empty()) {
      c.placement = {current_, "", cz::Side::End};
      c.line = local(lineLeft, rowsTop() - 1.0, lineWidth, 2.0);
      c.valid = true;
      return c;
    }
    for (const Range& r : sections) {
      if (ly < r.top || ly >= r.bottom) continue;
      const bool before = ly < r.top + (r.bottom - r.top) * 0.5;
      c.placement = {current_, r.id, before ? cz::Side::Before : cz::Side::After};
      c.line = local(lineLeft, (before ? r.top : r.bottom) - 1.0, lineWidth, 2.0);
      c.valid = true;
      return c;
    }
    // Below the last row: the end of the menu.
    c.placement = {current_, sections.back().id, cz::Side::After};
    c.line = local(lineLeft, endY - 1.0, lineWidth, 2.0);
    c.valid = true;
    return c;
  }

  // Entry-level payloads.
  if (rows_.empty()) {
    c.placement = {current_, "", cz::Side::End};  // a command added to a menu without sections gets one
    c.line = local(lineLeft, rowsTop() - 1.0, lineWidth, 2.0);
    c.valid = true;
    return c;
  }
  for (const Row& r : rows_) {
    if (ly < r.y || ly >= r.y + r.h) continue;
    const double indent = kLabelLeft + (r.depth + (r.kind == cz::Kind::Section ? 1 : 0)) * kIndent;
    if (r.kind == cz::Kind::Section) {
      c.placement = {r.id, "", cz::Side::Start};
      c.line = local(indent, r.y + r.h - 1.0, std::max(0.0, self.w - indent - 6.0), 2.0);
    } else {
      const bool before = ly < r.y + r.h * 0.5;
      c.placement = {r.parent, r.id, before ? cz::Side::Before : cz::Side::After};
      c.line = local(indent - 8.0, (before ? r.y : r.y + r.h) - 1.0, std::max(0.0, self.w - indent - 6.0 + 8.0), 2.0);
    }
    c.valid = true;
    return c;
  }
  // Below the last row: the end of the last top-level section.
  if (sections.empty()) return c;
  c.placement = {sections.back().id, "", cz::Side::End};
  c.line = local(kLabelLeft + kIndent - 8.0, endY - 1.0, std::max(0.0, self.w - kLabelLeft - kIndent - 6.0), 2.0);
  c.valid = true;
  return c;
}

bool MenuEditor::dragOver(const DragPayload& payload, double x, double y) {
  indicator_ = {};
  const Candidate c = locate(payload, x, y);
  if (!c.valid) {
    requestPaint();
    return false;
  }
  const cz::EditResult result = controller_.model().preview([&](cz::Customization& m) { return runDrop(m, payload, c.placement); });
  if (!result.ok) {
    if (result.error == cz::EditError::Locked) controller_.noteResult(result);
    requestPaint();
    return false;
  }
  indicator_.active = true;
  indicator_.line = c.line;
  indicator_.placement = c.placement;
  indicator_.title = c.title;
  requestPaint();
  return true;
}

void MenuEditor::dragLeave() {
  if (!indicator_.active) return;
  indicator_ = {};
  requestPaint();
}

bool MenuEditor::dragDrop(const DragPayload& payload, double x, double y) {
  indicator_ = {};
  const Candidate c = locate(payload, x, y);
  if (!c.valid) return false;
  const cz::EditResult result = runDrop(controller_.model(), payload, c.placement);
  controller_.noteResult(result);
  return result.ok;
}

}  // namespace r1ui::widgets
