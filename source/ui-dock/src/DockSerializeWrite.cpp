// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: DockLayout::toJson, the writer of layout schema version 2.
// Why: the writer is the only place that decides the byte layout of a stored layout, so a layout
//   that is loaded and saved again produces identical text (round-trip tests compare strings).
// Format (version 2): {"version":2,"name":S,"description":S,"main":{"root":NODE|null,"window":W},
//   "floating":[{"rect":R,"root":NODE,"window":W}],"panels":[{"id":N,"locked":B}],
//   "closed":[{"panel":N,"neighbours":[N],"index":N,"anchor":N,"side":S,"floating":B,"rect":R}]}.
//   NODE is {"type":"split","weight":w,["pinned":px,]["collapsed":true,]"axis":"row"|"column",
//   "children":[NODE]} or {"type":"stack","weight":w,...,"tabs":[N],"active":N}. W is {"maximized":B,
//   "monitor":S,"monitorIndex":N,"dpi":x[,"rect":R]}; R is {"x","y","w","h"}. "panels" lists every
//   panel the host knew when the layout was saved (docked or not), which is how a later load tells a
//   panel that is new from one the user closed.
// Failure behavior: numbers in a committed layout are always finite (validated at commit), so the
//   writer cannot throw on a valid DockLayout.
#include "DockJson.h"
#include "r1ui/dock/DockLayout.h"

namespace r1ui::dock {

namespace {

using detail::writeKey;

const char* sideName(Side side) {
  switch (side) {
    case Side::Left: return "left";
    case Side::Right: return "right";
    case Side::Top: return "top";
    case Side::Bottom: return "bottom";
  }
  return "left";
}

void writeNumberMember(std::string& out, const char* key, double value) {
  writeKey(out, key);
  core::appendNumber(out, value);
}

void writeBoolMember(std::string& out, const char* key, bool value) {
  writeKey(out, key);
  out += value ? "true" : "false";
}

void writeRect(std::string& out, const Rect& r) {
  out.push_back('{');
  writeNumberMember(out, "x", r.x);
  out.push_back(',');
  writeNumberMember(out, "y", r.y);
  out.push_back(',');
  writeNumberMember(out, "w", r.w);
  out.push_back(',');
  writeNumberMember(out, "h", r.h);
  out.push_back('}');
}

void writeNode(std::string& out, const Node& node) {
  out.push_back('{');
  writeKey(out, "type");
  core::appendQuoted(out, node.kind == Node::Kind::Split ? "split" : "stack");
  out.push_back(',');
  writeNumberMember(out, "weight", node.weight);
  out.push_back(',');
  if (node.pinned) {
    writeNumberMember(out, "pinned", node.pinnedSize);
    out.push_back(',');
  }
  if (node.collapsed) {
    writeBoolMember(out, "collapsed", true);
    out.push_back(',');
  }
  if (node.kind == Node::Kind::Split) {
    writeKey(out, "axis");
    core::appendQuoted(out, node.axis == Axis::Row ? "row" : "column");
    out.push_back(',');
    writeKey(out, "children");
    out.push_back('[');
    for (size_t i = 0; i < node.children.size(); ++i) {
      if (i > 0) out.push_back(',');
      writeNode(out, node.children[i]);
    }
    out.push_back(']');
  } else {
    writeKey(out, "tabs");
    out.push_back('[');
    for (size_t i = 0; i < node.tabs.size(); ++i) {
      if (i > 0) out.push_back(',');
      core::appendNumber(out, static_cast<double>(node.tabs[i]));
    }
    out += "],";
    writeNumberMember(out, "active", static_cast<double>(node.active));
  }
  out.push_back('}');
}

void writeRoot(std::string& out, const std::optional<Node>& root) {
  writeKey(out, "root");
  if (root) {
    writeNode(out, *root);
  } else {
    out += "null";
  }
}

void writeWindow(std::string& out, const WindowState& w) {
  writeKey(out, "window");
  out.push_back('{');
  writeBoolMember(out, "maximized", w.maximized);
  out.push_back(',');
  writeKey(out, "monitor");
  core::appendQuoted(out, w.monitor);
  out.push_back(',');
  writeNumberMember(out, "monitorIndex", static_cast<double>(w.monitorIndex));
  out.push_back(',');
  writeNumberMember(out, "dpi", w.dpiScale);
  if (w.hasRect) {
    out.push_back(',');
    writeKey(out, "rect");
    writeRect(out, w.rect);
  }
  out.push_back('}');
}

void writeClosed(std::string& out, const ClosedSlot& s) {
  out.push_back('{');
  writeNumberMember(out, "panel", static_cast<double>(s.panel));
  out.push_back(',');
  writeKey(out, "neighbours");
  out.push_back('[');
  for (size_t i = 0; i < s.neighbours.size(); ++i) {
    if (i > 0) out.push_back(',');
    core::appendNumber(out, static_cast<double>(s.neighbours[i]));
  }
  out += "],";
  writeNumberMember(out, "index", static_cast<double>(s.index));
  out.push_back(',');
  writeNumberMember(out, "anchor", static_cast<double>(s.anchor));
  out.push_back(',');
  writeKey(out, "side");
  core::appendQuoted(out, sideName(s.anchorSide));
  out.push_back(',');
  writeBoolMember(out, "floating", s.floating);
  if (s.floating) {
    out.push_back(',');
    writeKey(out, "rect");
    writeRect(out, s.floatRect);
  }
  out.push_back('}');
}

}  // namespace

std::string DockLayout::toJson() const {
  std::string out = "{";
  writeNumberMember(out, "version", 2.0);
  out.push_back(',');
  writeKey(out, "name");
  core::appendQuoted(out, meta_.name);
  out.push_back(',');
  writeKey(out, "description");
  core::appendQuoted(out, meta_.description);
  out.push_back(',');
  writeKey(out, "main");
  out.push_back('{');
  writeRoot(out, areas_.front().root);
  out.push_back(',');
  writeWindow(out, areas_.front().window);
  out += "},";
  writeKey(out, "floating");
  out.push_back('[');
  for (size_t i = 1; i < areas_.size(); ++i) {
    if (i > 1) out.push_back(',');
    out.push_back('{');
    writeKey(out, "rect");
    writeRect(out, areas_[i].rect);
    out.push_back(',');
    writeRoot(out, areas_[i].root);
    out.push_back(',');
    writeWindow(out, areas_[i].window);
    out.push_back('}');
  }
  out += "],";
  writeKey(out, "panels");
  out.push_back('[');
  for (size_t i = 0; i < panels_.size(); ++i) {
    if (i > 0) out.push_back(',');
    out.push_back('{');
    writeNumberMember(out, "id", static_cast<double>(panels_[i].id));
    out.push_back(',');
    writeBoolMember(out, "locked", panels_[i].locked);
    out.push_back('}');
  }
  out += "],";
  writeKey(out, "closed");
  out.push_back('[');
  for (size_t i = 0; i < closed_.size(); ++i) {
    if (i > 0) out.push_back(',');
    writeClosed(out, closed_[i]);
  }
  out += "]}";
  return out;
}

}  // namespace r1ui::dock
