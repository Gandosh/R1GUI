// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: layout persistence (spec 04): DockLayout::toJson and DockLayout::fromJson.
// Why: layout files are user-editable and may be damaged or hostile; every field is validated
//   here, at the file boundary, before any DockLayout exists, so a bad file cannot reach the
//   tree and the caller's current layout is never modified (fromJson is a factory).
// Format (version 1): {"version":1,"main":{"root":NODE|null},"floating":[{"rect":{x,y,w,h},
//   "root":NODE}]}; NODE is {"type":"split","weight":w,"axis":"row"|"column","children":[NODE..]}
//   or {"type":"stack","weight":w,"tabs":[panelId..],"active":i}. Unknown members are rejected.
// Failure behavior: wrong version, missing/extra fields, wrong types, non-integral or out-of-range
//   ids, duplicate ids, bad weights, depth/node/area limits all produce an error string. Panels
//   the host no longer registers are dropped (and counted), then the tree is normalised.
// Our decisions: dropped panels are not remembered for later (spec 04 rule 39 keeps them in the
//   file); a floating area left with no known panel is discarded (rule 40 keeps it collapsed);
//   a layout with no docked panel at all is rejected as unusable (rule 14).
#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "DockTree.h"
#include "r1ui/core/CheckedCast.h"
#include "r1ui/core/Json.h"
#include "r1ui/core/JsonWriter.h"
#include "r1ui/dock/DockLayout.h"

namespace r1ui::dock {

namespace {

using core::JsonType;
using core::JsonValue;

constexpr int kFormatVersion = 1;

// ---- Writing ----------------------------------------------------------------------------

void writeKey(std::string& out, const char* key) {
  core::appendQuoted(out, key);
  out.push_back(':');
}

void writeNode(std::string& out, const Node& node) {
  out.push_back('{');
  writeKey(out, "type");
  core::appendQuoted(out, node.kind == Node::Kind::Split ? "split" : "stack");
  out.push_back(',');
  writeKey(out, "weight");
  core::appendNumber(out, node.weight);
  out.push_back(',');
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
    writeKey(out, "active");
    core::appendNumber(out, static_cast<double>(node.active));
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

// ---- Reading ----------------------------------------------------------------------------

// Everything the node reader needs to carry through the recursion.
struct ReadContext {
  const std::unordered_set<PanelId>& known;
  std::unordered_set<PanelId> seen;  // every id in the file, known or not
  size_t nodes = 0;
  size_t dropped = 0;
};

// Rejects members other than `allowed` so a typo or newer field cannot be silently ignored.
std::string checkMembers(const JsonValue& object, std::initializer_list<const char*> allowed, const char* what) {
  for (size_t i = 0; i < object.size(); ++i) {
    bool found = false;
    for (const char* name : allowed) found = found || object.keyAt(i) == name;
    if (!found) return std::string(what) + " has an unknown member \"" + object.keyAt(i) + "\"";
  }
  return {};
}

const JsonValue* member(const JsonValue& object, const char* name, JsonType type, std::string& error, const char* what) {
  const JsonValue* value = object.find(name);
  if (value == nullptr) {
    error = std::string(what) + " is missing \"" + name + "\"";
    return nullptr;
  }
  if (value->type() != type) {
    error = std::string(what) + " member \"" + name + "\" has the wrong type";
    return nullptr;
  }
  return value;
}

bool readWeight(const JsonValue& object, double& weight, std::string& error) {
  const JsonValue* value = member(object, "weight", JsonType::Number, error, "node");
  if (value == nullptr) return false;
  weight = value->numberValue();
  if (!(weight >= kMinWeight && weight <= kMaxWeight)) {  // also rejects NaN
    error = "node weight is not a positive number in range";
    return false;
  }
  return true;
}

// Reads a non-negative integer that fits `limit`.
bool readIndex(const JsonValue& value, double limit, double& out) {
  if (value.type() != JsonType::Number) return false;
  const double v = value.numberValue();
  if (!(v >= 0.0 && v <= limit) || v != std::floor(v)) return false;
  out = v;
  return true;
}

bool readNode(const JsonValue& json, size_t depth, ReadContext& ctx, Node& out, std::string& error) {
  if (depth > kMaxTreeDepth) {
    error = "dock tree is nested deeper than the limit";
    return false;
  }
  if (++ctx.nodes > kMaxTreeNodes) {
    error = "dock tree has more nodes than the limit";
    return false;
  }
  if (!json.isObject()) {
    error = "node is not an object";
    return false;
  }
  const JsonValue* type = member(json, "type", JsonType::String, error, "node");
  if (type == nullptr || !readWeight(json, out.weight, error)) return false;

  if (type->stringValue() == "split") {
    error = checkMembers(json, {"type", "weight", "axis", "children"}, "split");
    if (!error.empty()) return false;
    const JsonValue* axis = member(json, "axis", JsonType::String, error, "split");
    const JsonValue* children = member(json, "children", JsonType::Array, error, "split");
    if (axis == nullptr || children == nullptr) return false;
    if (axis->stringValue() != "row" && axis->stringValue() != "column") {
      error = "split axis must be \"row\" or \"column\"";
      return false;
    }
    if (children->size() == 0) {
      error = "split has no children";
      return false;
    }
    out.kind = Node::Kind::Split;
    out.axis = axis->stringValue() == "row" ? Axis::Row : Axis::Column;
    out.children.reserve(children->size());
    for (size_t i = 0; i < children->size(); ++i) {
      Node child;
      if (!readNode(children->child(i), depth + 1, ctx, child, error)) return false;
      out.children.push_back(std::move(child));
    }
    return true;
  }
  if (type->stringValue() == "stack") {
    error = checkMembers(json, {"type", "weight", "tabs", "active"}, "stack");
    if (!error.empty()) return false;
    const JsonValue* tabs = member(json, "tabs", JsonType::Array, error, "stack");
    const JsonValue* active = member(json, "active", JsonType::Number, error, "stack");
    if (tabs == nullptr || active == nullptr) return false;
    if (tabs->size() == 0) {
      error = "stack has no tabs";
      return false;
    }
    double activeIndex = 0.0;
    if (!readIndex(*active, static_cast<double>(tabs->size() - 1), activeIndex)) {
      error = "stack active index is not a valid tab index";
      return false;
    }
    out.kind = Node::Kind::Stack;
    size_t survivorsBeforeActive = 0;
    for (size_t i = 0; i < tabs->size(); ++i) {
      double id = 0.0;
      if (!readIndex(tabs->child(i), static_cast<double>(UINT32_MAX), id)) {
        error = "panel id is not an integer in range";
        return false;
      }
      const PanelId panel = static_cast<PanelId>(id);
      if (!ctx.seen.insert(panel).second) {
        error = "panel " + std::to_string(panel) + " appears twice in the layout";
        return false;
      }
      if (ctx.known.count(panel) == 0) {
        ++ctx.dropped;  // the host no longer has this panel (spec 04 rule 39)
        continue;
      }
      if (static_cast<double>(i) < activeIndex) ++survivorsBeforeActive;
      out.tabs.push_back(panel);
    }
    out.active = survivorsBeforeActive;  // the next surviving tab takes over a dropped front tab
    return true;
  }
  error = "node type must be \"split\" or \"stack\"";
  return false;
}

// Reads the optional root of an area; "root": null means empty.
bool readRoot(const JsonValue& area, ReadContext& ctx, std::optional<Node>& root, std::string& error) {
  const JsonValue* json = area.find("root");
  if (json == nullptr) {
    error = "area is missing \"root\"";
    return false;
  }
  if (json->type() == JsonType::Null) return true;
  Node node;
  if (!readNode(*json, 1, ctx, node, error)) return false;
  root = std::move(node);
  return true;
}

bool readRect(const JsonValue& json, Rect& rect, std::string& error) {
  const JsonValue* object = member(json, "rect", JsonType::Object, error, "floating area");
  if (object == nullptr) return false;
  error = checkMembers(*object, {"x", "y", "w", "h"}, "rect");
  if (!error.empty()) return false;
  double values[4] = {};
  const char* names[4] = {"x", "y", "w", "h"};
  for (int i = 0; i < 4; ++i) {
    const JsonValue* v = member(*object, names[i], JsonType::Number, error, "rect");
    if (v == nullptr) return false;
    values[i] = v->numberValue();
  }
  rect = {values[0], values[1], values[2], values[3]};
  return true;
}

}  // namespace

std::string DockLayout::toJson() const {
  std::string out = "{";
  writeKey(out, "version");
  core::appendNumber(out, kFormatVersion);
  out.push_back(',');
  writeKey(out, "main");
  out.push_back('{');
  writeRoot(out, areas_.front().root);
  out += "},";
  writeKey(out, "floating");
  out.push_back('[');
  for (size_t i = 1; i < areas_.size(); ++i) {
    if (i > 1) out.push_back(',');
    const Rect& r = areas_[i].rect;
    out += "{";
    writeKey(out, "rect");
    out.push_back('{');
    writeKey(out, "x");
    core::appendNumber(out, r.x);
    out.push_back(',');
    writeKey(out, "y");
    core::appendNumber(out, r.y);
    out.push_back(',');
    writeKey(out, "w");
    core::appendNumber(out, r.w);
    out.push_back(',');
    writeKey(out, "h");
    core::appendNumber(out, r.h);
    out += "},";
    writeRoot(out, areas_[i].root);
    out.push_back('}');
  }
  out += "]}";
  return out;
}

LoadResult DockLayout::fromJson(std::string_view text, std::vector<PanelInfo> panels, const DockConfig& config) {
  LoadResult result;
  const auto fail = [&](std::string message) {
    result.error = std::move(message);
    return std::move(result);
  };

  core::JsonLimits limits;
  limits.maxDepth = 96;  // two JSON levels per tree level plus the wrapper objects
  limits.maxInputBytes = size_t{4} * 1024 * 1024;
  const core::JsonResult parsed = core::parseJson(text, limits);
  if (!parsed.ok()) {
    return fail("layout is not valid JSON: " + parsed.error.message + " (offset " + std::to_string(parsed.error.offset) + ")");
  }
  const JsonValue& doc = *parsed.value;
  std::string error;
  if (!doc.isObject()) return fail("layout is not a JSON object");
  error = checkMembers(doc, {"version", "main", "floating"}, "layout");
  if (!error.empty()) return fail(error);
  const JsonValue* version = member(doc, "version", JsonType::Number, error, "layout");
  if (version == nullptr) return fail(error);
  if (version->numberValue() != static_cast<double>(kFormatVersion)) {
    return fail("unsupported layout version (this build reads version " + std::to_string(kFormatVersion) + ")");
  }
  const JsonValue* main = member(doc, "main", JsonType::Object, error, "layout");
  const JsonValue* floating = main != nullptr ? member(doc, "floating", JsonType::Array, error, "layout") : nullptr;
  if (main == nullptr || floating == nullptr) return fail(error);
  error = checkMembers(*main, {"root"}, "main area");
  if (!error.empty()) return fail(error);
  if (floating->size() > kMaxFloatingAreas) return fail("layout has too many floating areas");

  std::unordered_set<PanelId> known;
  for (const PanelInfo& p : panels) known.insert(p.id);
  ReadContext ctx{known, {}, 0, 0};

  std::vector<Area> areas;
  Area mainArea;
  mainArea.id = kMainAreaId;
  if (!readRoot(*main, ctx, mainArea.root, error)) return fail(error);
  areas.push_back(std::move(mainArea));
  for (size_t i = 0; i < floating->size(); ++i) {
    const JsonValue& json = floating->child(i);
    if (!json.isObject()) return fail("floating area is not an object");
    error = checkMembers(json, {"rect", "root"}, "floating area");
    if (!error.empty()) return fail(error);
    Area area;
    area.id = core::checkedCast<uint32_t>(i + 1);
    if (!readRect(json, area.rect, error) || !readRoot(json, ctx, area.root, error)) return fail(error);
    if (!area.root) return fail("floating area has no root");
    areas.push_back(std::move(area));
  }

  DockLayoutResult created = DockLayout::create(std::move(panels), config);
  if (!created.ok()) return fail(created.error);
  DockLayout layout = std::move(*created.layout);
  Status status = layout.commit(std::move(areas), core::checkedCast<uint32_t>(floating->size() + 1));
  if (!status) return fail(status.error);
  const bool anyDocked = std::any_of(layout.panels_.begin(), layout.panels_.end(),
                                     [&](const PanelInfo& p) { return layout.isDocked(p.id); });
  if (!anyDocked) return fail("layout has no usable panels");
  result.layout = std::move(layout);
  result.droppedPanels = ctx.dropped;
  return result;
}

}  // namespace r1ui::dock
