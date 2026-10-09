// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of customize/Layout.h: node builders, kind names, the hierarchy rules
//   (canContain), size steps and validateLayout.
// Invariants: all functions are pure; validateLayout never recurses deeper than kMaxDepth + 1 levels
//   (it stops descending past the limit and reports it), so a hostile tree cannot overflow the stack.
// Callers: Effective.cpp, Customization*.cpp, CustomizationIo.cpp, tests.
#include "r1ui/commands/customize/Layout.h"

#include <cmath>
#include <unordered_set>

#include "r1ui/commands/Text.h"

namespace r1ui::commands::customize {

const char* kindName(Kind kind) {
  switch (kind) {
    case Kind::MenuBar: return "menubar";
    case Kind::Toolbar: return "toolbar";
    case Kind::Panel: return "panel";
    case Kind::Menu: return "menu";
    case Kind::Section: return "section";
    case Kind::Command: return "command";
    case Kind::Separator: return "separator";
    case Kind::Heading: return "heading";
    case Kind::Submenu: return "submenu";
    case Kind::Group: return "group";
    case Kind::Spacer: return "spacer";
    case Kind::FreeButton: return "button";
  }
  return "?";
}

bool parseNodeKind(std::string_view name, Kind& kind) {
  for (const Kind k : {Kind::Menu, Kind::Section, Kind::Command, Kind::Separator, Kind::Heading, Kind::Submenu, Kind::Group, Kind::Spacer, Kind::FreeButton}) {
    if (name == kindName(k)) {
      kind = k;
      return true;
    }
  }
  return false;
}

bool canContain(Kind parent, Kind child) {
  switch (parent) {
    case Kind::MenuBar: return child == Kind::Menu;
    case Kind::Menu:
    case Kind::Submenu: return child == Kind::Section;
    case Kind::Section: return child == Kind::Command || child == Kind::Separator || child == Kind::Heading || child == Kind::Submenu;
    case Kind::Toolbar: return child == Kind::Command || child == Kind::Separator || child == Kind::Group || child == Kind::Spacer;
    case Kind::Group: return child == Kind::Command;
    case Kind::Panel: return child == Kind::FreeButton;
    default: return false;
  }
}

// ---- builders -----------------------------------------------------------------------------------

namespace {

Node make(Kind kind, std::string id) {
  Node node;
  node.kind = kind;
  node.id = std::move(id);
  return node;
}

}  // namespace

Node Node::menu(std::string id, std::string title, std::vector<Node> sections) {
  Node n = make(Kind::Menu, std::move(id));
  n.label = std::move(title);
  n.children = std::move(sections);
  return n;
}

Node Node::section(std::string id, std::string heading, std::vector<Node> entries) {
  Node n = make(Kind::Section, std::move(id));
  n.label = std::move(heading);
  n.children = std::move(entries);
  return n;
}

Node Node::command(std::string id, std::string commandId) {
  Node n = make(Kind::Command, std::move(id));
  n.commandId = std::move(commandId);
  return n;
}

Node Node::separator(std::string id) { return make(Kind::Separator, std::move(id)); }

Node Node::heading(std::string id, std::string text) {
  Node n = make(Kind::Heading, std::move(id));
  n.label = std::move(text);
  return n;
}

Node Node::submenu(std::string id, std::string title, std::vector<Node> sections) {
  Node n = make(Kind::Submenu, std::move(id));
  n.label = std::move(title);
  n.children = std::move(sections);
  return n;
}

Node Node::group(std::string id, std::vector<Node> commands) {
  Node n = make(Kind::Group, std::move(id));
  n.children = std::move(commands);
  return n;
}

Node Node::spacer(std::string id) { return make(Kind::Spacer, std::move(id)); }

Node Node::freeButton(std::string id, std::string commandId, Rect rect) {
  Node n = make(Kind::FreeButton, std::move(id));
  n.commandId = std::move(commandId);
  n.rect = rect;
  return n;
}

// ---- size steps ---------------------------------------------------------------------------------

const char* sizeStepName(SizeStep step) {
  switch (step) {
    case SizeStep::Small: return "small";
    case SizeStep::Medium: return "medium";
    case SizeStep::Large: return "large";
  }
  return "medium";
}

bool parseSizeStep(std::string_view name, SizeStep& step) {
  for (const SizeStep s : {SizeStep::Small, SizeStep::Medium, SizeStep::Large}) {
    if (name == sizeStepName(s)) {
      step = s;
      return true;
    }
  }
  return false;
}

double sizeStepPixels(SizeStep step) {
  switch (step) {
    case SizeStep::Small: return 26.0;
    case SizeStep::Medium: return 32.0;
    case SizeStep::Large: return 40.0;
  }
  return 32.0;
}

// ---- queries ------------------------------------------------------------------------------------

namespace {

size_t countIn(const std::vector<Node>& nodes) {
  size_t total = 0;
  for (const Node& n : nodes) total += 1 + countIn(n.children);
  return total;
}

const Node* findIn(const std::vector<Node>& nodes, std::string_view id) {
  for (const Node& n : nodes) {
    if (n.id == id) return &n;
    if (const Node* inner = findIn(n.children, id)) return inner;
  }
  return nullptr;
}

}  // namespace

size_t countNodes(const LayoutSet& set) {
  size_t total = countIn(set.menuBar.menus);
  for (const ToolbarLayout& t : set.toolbars) total += countIn(t.items);
  for (const FreeFormPanelLayout& p : set.panels) total += countIn(p.buttons);
  return total;
}

const Node* findNode(const LayoutSet& set, std::string_view id) {
  if (const Node* n = findIn(set.menuBar.menus, id)) return n;
  for (const ToolbarLayout& t : set.toolbars) {
    if (const Node* n = findIn(t.items, id)) return n;
  }
  for (const FreeFormPanelLayout& p : set.panels) {
    if (const Node* n = findIn(p.buttons, id)) return n;
  }
  return nullptr;
}

const ToolbarLayout* findToolbar(const LayoutSet& set, std::string_view id) {
  for (const ToolbarLayout& t : set.toolbars) {
    if (t.id == id) return &t;
  }
  return nullptr;
}

const FreeFormPanelLayout* findPanel(const LayoutSet& set, std::string_view id) {
  for (const FreeFormPanelLayout& p : set.panels) {
    if (p.id == id) return &p;
  }
  return nullptr;
}

bool isValidNodeId(std::string_view id) { return isValidIdentifier(id, kMaxIdBytes); }

// ---- validation ---------------------------------------------------------------------------------

namespace {

struct Validator {
  std::vector<std::string> problems;
  std::unordered_set<std::string> ids;
  size_t nodes = 0;

  void add(std::string text) {
    if (problems.size() < 64) problems.push_back(std::move(text));
  }

  bool claimId(const std::string& id) {
    if (!isValidNodeId(id)) {
      add("invalid id '" + id.substr(0, 40) + "'");
      return false;
    }
    if (!ids.insert(id).second) {
      add("duplicate id '" + id + "'");
      return false;
    }
    return true;
  }

  void nodeList(Kind parent, const std::vector<Node>& list, size_t depth) {
    for (const Node& n : list) {
      ++nodes;
      claimId(n.id);
      if (!canContain(parent, n.kind)) add("illegal child '" + n.id + "' (" + kindName(n.kind) + ") in a " + kindName(parent));
      if (n.label.size() > kMaxLabelBytes || n.userLabel.size() > kMaxLabelBytes) add("text too long in '" + n.id + "'");
      if (n.kind == Kind::FreeButton) {
        const Rect& r = n.rect;
        if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.w) || !std::isfinite(r.h) || r.w < kMinButtonSize || r.h < kMinButtonSize) {
          add("bad rectangle in '" + n.id + "'");
        }
      }
      if (!n.children.empty()) {
        if (depth + 1 > kMaxDepth) {
          add("nesting deeper than " + std::to_string(kMaxDepth) + " at '" + n.id + "'");
        } else {
          nodeList(n.kind, n.children, depth + 1);
        }
      }
    }
  }
};

}  // namespace

std::vector<std::string> validateLayout(const LayoutSet& set) {
  Validator v;
  v.claimId(set.menuBar.id);
  v.nodeList(Kind::MenuBar, set.menuBar.menus, 1);
  for (const ToolbarLayout& t : set.toolbars) {
    v.claimId(t.id);
    if (!std::isfinite(t.gap) || t.gap < kMinToolbarGap || t.gap > kMaxToolbarGap) v.add("bad gap in toolbar '" + t.id + "'");
    v.nodeList(Kind::Toolbar, t.items, 1);
  }
  for (const FreeFormPanelLayout& p : set.panels) {
    v.claimId(p.id);
    if (!std::isfinite(p.width) || !std::isfinite(p.height) || p.width <= 0.0 || p.height <= 0.0 || p.width > kMaxPanelSize || p.height > kMaxPanelSize) {
      v.add("bad size in panel '" + p.id + "'");
    }
    if (!std::isfinite(p.grid) || p.grid < 1.0 || p.grid > 256.0) v.add("bad grid in panel '" + p.id + "'");
    v.nodeList(Kind::Panel, p.buttons, 1);
  }
  if (v.nodes > kMaxNodes) v.add("more than " + std::to_string(kMaxNodes) + " nodes");
  return std::move(v.problems);
}

}  // namespace r1ui::commands::customize
