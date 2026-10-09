// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: exportDelta (CustomizationIo.h): the deterministic JSON text of a Delta.
// Invariants: output is valid UTF-8 JSON in a fixed member and entry order (ordered maps), one entry per
//   line, and parses back to an equal Delta (the round trip test covers it).
// Callers: CustomizationStorage, hosts, tests.
#include "r1ui/commands/customize/CustomizationIo.h"

#include "r1ui/core/JsonWriter.h"

namespace r1ui::commands::customize {

namespace {

// ---- export -------------------------------------------------------------------------------------

void key(std::string& out, const char* name) {
  core::appendQuoted(out, name);
  out += ':';
}

void quoted(std::string& out, std::string_view text) { core::appendQuoted(out, text); }

void number(std::string& out, double value) { core::appendNumber(out, value); }

void rectJson(std::string& out, const Rect& r) {
  out += '[';
  number(out, r.x);
  out += ',';
  number(out, r.y);
  out += ',';
  number(out, r.w);
  out += ',';
  number(out, r.h);
  out += ']';
}

template <class T, class Fn>
void list(std::string& out, const char* name, const std::vector<T>& items, Fn writeItem) {
  out += ",\n";
  key(out, name);
  out += '[';
  bool first = true;
  for (const T& item : items) {
    out += first ? "\n" : ",\n";
    first = false;
    writeItem(item);
  }
  out += ']';
}

}  // namespace

std::string exportDelta(const Delta& d) {
  std::string out = "{";
  key(out, "format");
  quoted(out, kFormatName);
  out += ",\n";
  key(out, "version");
  out += std::to_string(kFormatVersion);
  out += ",\n";
  key(out, "serial");
  out += std::to_string(d.serial);

  struct EditRow {
    const std::string* id;
    const NodeEdit* edit;
  };
  std::vector<EditRow> edits;
  for (const auto& [id, edit] : d.edits) edits.push_back({&id, &edit});
  list(out, "edits", edits, [&](const EditRow& row) {
    out += '{';
    key(out, "node");
    quoted(out, *row.id);
    if (row.edit->hidden) {
      out += ',';
      key(out, "hidden");
      out += *row.edit->hidden ? "true" : "false";
    }
    if (row.edit->label) {
      out += ',';
      key(out, "label");
      quoted(out, *row.edit->label);
    }
    if (row.edit->rect) {
      out += ',';
      key(out, "rect");
      rectJson(out, *row.edit->rect);
    }
    out += '}';
  });
  list(out, "moves", d.moves, [&](const MoveEdit& m) {
    out += '{';
    key(out, "node");
    quoted(out, m.node);
    out += ',';
    key(out, "parent");
    quoted(out, m.to.parent);
    out += ',';
    key(out, "anchor");
    quoted(out, m.to.anchor);
    out += ',';
    key(out, "side");
    quoted(out, sideName(m.to.side));
    out += '}';
  });
  list(out, "added", d.added, [&](const AddedNode& a) {
    out += '{';
    key(out, "id");
    quoted(out, a.node.id);
    out += ',';
    key(out, "kind");
    quoted(out, kindName(a.node.kind));
    if (!a.node.commandId.empty()) {
      out += ',';
      key(out, "command");
      quoted(out, a.node.commandId);
    }
    if (!a.node.label.empty()) {
      out += ',';
      key(out, "label");
      quoted(out, a.node.label);
    }
    if (!a.node.visible) out += ",\"hidden\":true";
    if (a.node.kind == Kind::FreeButton) {
      out += ',';
      key(out, "rect");
      rectJson(out, a.node.rect);
    }
    out += ',';
    key(out, "parent");
    quoted(out, a.at.parent);
    out += ',';
    key(out, "anchor");
    quoted(out, a.at.anchor);
    out += ',';
    key(out, "side");
    quoted(out, sideName(a.at.side));
    out += '}';
  });
  list(out, "toolbars", d.userToolbars, [&](const ToolbarLayout& t) {
    out += '{';
    key(out, "id");
    quoted(out, t.id);
    out += ',';
    key(out, "title");
    quoted(out, t.title);
    out += ',';
    key(out, "orientation");
    quoted(out, t.orientation == Orientation::Vertical ? "vertical" : "horizontal");
    out += ',';
    key(out, "sizeStep");
    quoted(out, sizeStepName(t.sizeStep));
    out += ',';
    key(out, "gap");
    number(out, t.gap);
    out += '}';
  });
  list(out, "panels", d.userPanels, [&](const FreeFormPanelLayout& p) {
    out += '{';
    key(out, "id");
    quoted(out, p.id);
    out += ',';
    key(out, "title");
    quoted(out, p.title);
    out += ',';
    key(out, "width");
    number(out, p.width);
    out += ',';
    key(out, "height");
    number(out, p.height);
    out += ',';
    key(out, "snap");
    out += p.snap ? "true" : "false";
    out += ',';
    key(out, "grid");
    number(out, p.grid);
    out += '}';
  });
  struct ToolbarRow {
    const std::string* id;
    const ToolbarEdit* edit;
  };
  std::vector<ToolbarRow> toolbarEdits;
  for (const auto& [id, edit] : d.toolbarEdits) toolbarEdits.push_back({&id, &edit});
  list(out, "toolbarEdits", toolbarEdits, [&](const ToolbarRow& row) {
    out += '{';
    key(out, "id");
    quoted(out, *row.id);
    if (row.edit->sizeStep) {
      out += ',';
      key(out, "sizeStep");
      quoted(out, sizeStepName(*row.edit->sizeStep));
    }
    if (row.edit->gap) {
      out += ',';
      key(out, "gap");
      number(out, *row.edit->gap);
    }
    out += '}';
  });
  struct PanelRow {
    const std::string* id;
    const PanelEdit* edit;
  };
  std::vector<PanelRow> panelEdits;
  for (const auto& [id, edit] : d.panelEdits) panelEdits.push_back({&id, &edit});
  list(out, "panelEdits", panelEdits, [&](const PanelRow& row) {
    out += '{';
    key(out, "id");
    quoted(out, *row.id);
    if (row.edit->snap) {
      out += ',';
      key(out, "snap");
      out += *row.edit->snap ? "true" : "false";
    }
    if (row.edit->grid) {
      out += ',';
      key(out, "grid");
      number(out, *row.edit->grid);
    }
    out += '}';
  });
  out += "\n}\n";
  return out;
}

}  // namespace r1ui::commands::customize
