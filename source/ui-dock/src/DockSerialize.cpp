// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: layout reading (spec 04): DockLayout::fromJson for schema versions 1 and 2, the shared
//   JSON helpers and the repair policy of the lenient (version 2) reader.
// Why: layout files are user-editable and may be damaged or hostile; every field is validated
//   here, at the file boundary, before any DockLayout exists, so a bad file cannot reach the
//   tree and the caller's current layout is never modified (fromJson is a factory).
// Versions: 1 is read strictly (any defect rejects the file, as it always did). 2 is read
//   leniently where a defect is local and repairable: unknown, duplicate or invalid panel ids are
//   dropped, weights outside the legal range, active indexes, sizes, monitor names and strings are
//   repaired, a damaged closed-panel record is dropped; each repair is counted and described in
//   LoadResult::warnings. Structural defects (wrong types, unknown members, depth or node limits,
//   more than the allowed windows, a layout with nothing to show) reject the whole file. A version
//   other than 1 or 2 is rejected cleanly so the caller keeps its previous layout. The writer is
//   DockSerializeWrite.cpp; the format is documented there.
// Our decisions: a layout with no docked panel at all is rejected as unusable (spec 04 rule 14);
//   a floating area left with no known panel is discarded (rule 40 keeps it collapsed); panels
//   the host no longer has are not remembered for later (rule 39), except that the host passes the
//   registry it has now, so a panel that returns is simply new again.
#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "DockJson.h"
#include "DockTree.h"
#include "r1ui/core/CheckedCast.h"
#include "r1ui/dock/DockLayout.h"

namespace r1ui::dock {

namespace detail {

std::string checkMembers(const core::JsonValue& object, std::initializer_list<const char*> allowed, const char* what) {
  for (size_t i = 0; i < object.size(); ++i) {
    bool found = false;
    for (const char* name : allowed) found = found || object.keyAt(i) == name;
    if (!found) return std::string(what) + " has an unknown member \"" + object.keyAt(i) + "\"";
  }
  return {};
}

const core::JsonValue* member(const core::JsonValue& object, const char* name, core::JsonType type, std::string& error,
                              const char* what) {
  const core::JsonValue* value = object.find(name);
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

bool readIndex(const core::JsonValue& value, double limit, double& out) {
  if (value.type() != core::JsonType::Number) return false;
  const double v = value.numberValue();
  if (!(v >= 0.0 && v <= limit) || v != std::floor(v)) return false;
  out = v;
  return true;
}

std::string cleanText(std::string_view text, size_t maxBytes) {
  size_t end = std::min(text.size(), maxBytes);
  // Never cut inside a multi-byte sequence: back up to a lead byte.
  while (end > 0 && end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
  std::string out(text.substr(0, end));
  for (char& c : out) {
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = ' ';
  }
  return out;
}

}  // namespace detail

namespace {

using core::JsonType;
using core::JsonValue;
using detail::checkMembers;
using detail::member;
using detail::readIndex;

constexpr int kReadableVersions = 2;
constexpr size_t kMaxWarnings = 100;

// Everything the readers carry through the recursion.
struct ReadContext {
  const std::unordered_set<PanelId>& known;
  bool strict = true;  // version 1: reject instead of repairing
  std::unordered_set<PanelId> seen;  // every id in the trees, known or not
  size_t nodes = 0;
  size_t dropped = 0;
  size_t duplicates = 0;
  size_t repaired = 0;
  std::vector<std::string>& warnings;

  void warn(std::string message) {
    if (warnings.size() < kMaxWarnings) {
      warnings.push_back(std::move(message));
    } else if (warnings.size() == kMaxWarnings) {
      warnings.push_back("further repairs are not listed");
    }
  }
  void repair(std::string message) {
    ++repaired;
    warn(std::move(message));
  }
};

// ---- trees ---------------------------------------------------------------------------------

bool readWeight(const JsonValue& object, ReadContext& ctx, double& weight, std::string& error) {
  const JsonValue* value = member(object, "weight", JsonType::Number, error, "node");
  if (value == nullptr) return false;
  weight = value->numberValue();
  if (weight >= kMinWeight && weight <= kMaxWeight) return true;
  if (ctx.strict) {  // also rejects NaN
    error = "node weight is not a positive number in range";
    return false;
  }
  weight = weight > kMaxWeight ? kMaxWeight : (weight > 0.0 ? kMinWeight : 1.0);
  ctx.repair("a node weight outside the legal range was repaired");
  return true;
}

// Reads the optional "pinned" (pixels) and "collapsed" (true) members of a version 2 node.
bool readNodeFlags(const JsonValue& json, ReadContext& ctx, Node& out, std::string& error) {
  if (const JsonValue* pinned = json.find("pinned")) {
    if (pinned->type() != JsonType::Number) {
      error = "node member \"pinned\" has the wrong type";
      return false;
    }
    const double px = pinned->numberValue();
    if (px >= 0.0 && px <= kMaxCoordinate) {
      out.pinned = true;
      out.pinnedSize = px;
    } else {
      ctx.repair("a pinned size outside the legal range was dropped");
    }
  }
  if (const JsonValue* collapsed = json.find("collapsed")) {
    if (collapsed->type() != JsonType::Bool) {
      error = "node member \"collapsed\" has the wrong type";
      return false;
    }
    out.collapsed = collapsed->boolValue();
  }
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
  if (type == nullptr || !readWeight(json, ctx, out.weight, error)) return false;
  if (!ctx.strict && !readNodeFlags(json, ctx, out, error)) return false;

  if (type->stringValue() == "split") {
    error = ctx.strict ? checkMembers(json, {"type", "weight", "axis", "children"}, "split")
                       : checkMembers(json, {"type", "weight", "pinned", "collapsed", "axis", "children"}, "split");
    if (!error.empty()) return false;
    const JsonValue* axis = member(json, "axis", JsonType::String, error, "split");
    const JsonValue* children = member(json, "children", JsonType::Array, error, "split");
    if (axis == nullptr || children == nullptr) return false;
    if (axis->stringValue() != "row" && axis->stringValue() != "column") {
      error = "split axis must be \"row\" or \"column\"";
      return false;
    }
    if (children->size() == 0 && ctx.strict) {
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
    error = ctx.strict ? checkMembers(json, {"type", "weight", "tabs", "active"}, "stack")
                       : checkMembers(json, {"type", "weight", "pinned", "collapsed", "tabs", "active"}, "stack");
    if (!error.empty()) return false;
    const JsonValue* tabs = member(json, "tabs", JsonType::Array, error, "stack");
    const JsonValue* active = member(json, "active", JsonType::Number, error, "stack");
    if (tabs == nullptr || active == nullptr) return false;
    if (tabs->size() == 0 && ctx.strict) {
      error = "stack has no tabs";
      return false;
    }
    double activeIndex = 0.0;
    if (!readIndex(*active, tabs->size() == 0 ? 0.0 : static_cast<double>(tabs->size() - 1), activeIndex)) {
      if (ctx.strict) {
        error = "stack active index is not a valid tab index";
        return false;
      }
      const double raw = active->numberValue();
      activeIndex = tabs->size() == 0 ? 0.0 : std::clamp(std::floor(raw), 0.0, static_cast<double>(tabs->size() - 1));
      ctx.repair("a stack's active index was out of range and was clamped");
    }
    out.kind = Node::Kind::Stack;
    size_t survivorsBeforeActive = 0;
    for (size_t i = 0; i < tabs->size(); ++i) {
      double id = 0.0;
      if (!readIndex(tabs->child(i), static_cast<double>(UINT32_MAX), id)) {
        if (ctx.strict) {
          error = "panel id is not an integer in range";
          return false;
        }
        ctx.repair("a tab that is not a valid panel id was dropped");
        continue;
      }
      const PanelId panel = static_cast<PanelId>(id);
      if (!ctx.seen.insert(panel).second) {
        if (ctx.strict) {
          error = "panel " + std::to_string(panel) + " appears twice in the layout";
          return false;
        }
        ++ctx.duplicates;
        ctx.warn("panel " + std::to_string(panel) + " appeared more than once; the extra tab was dropped");
        continue;
      }
      if (ctx.known.count(panel) == 0) {
        ++ctx.dropped;  // the host no longer has this panel (spec 04 rule 39)
        ctx.warn("panel " + std::to_string(panel) + " is not registered and was dropped");
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

// ---- rectangles, windows, closed slots -------------------------------------------------------------

bool readRectObject(const JsonValue& object, Rect& rect, std::string& error) {
  error = checkMembers(object, {"x", "y", "w", "h"}, "rect");
  if (!error.empty()) return false;
  double values[4] = {};
  const char* names[4] = {"x", "y", "w", "h"};
  for (int i = 0; i < 4; ++i) {
    const JsonValue* v = member(object, names[i], JsonType::Number, error, "rect");
    if (v == nullptr) return false;
    values[i] = v->numberValue();
  }
  rect = {values[0], values[1], values[2], values[3]};
  return true;
}

bool readRect(const JsonValue& json, Rect& rect, std::string& error, const char* what) {
  const JsonValue* object = member(json, "rect", JsonType::Object, error, what);
  return object != nullptr && readRectObject(*object, rect, error);
}

bool rectInRange(const Rect& r) {
  return std::abs(r.x) <= kMaxCoordinate && std::abs(r.y) <= kMaxCoordinate && r.w > 0.0 && r.h > 0.0 &&
         r.w <= kMaxCoordinate && r.h <= kMaxCoordinate;
}

// Brings a floating rectangle into the legal range instead of rejecting the file (version 2).
bool repairFloatRect(Rect& r, const DockConfig& config) {
  const Rect before = r;
  r.x = std::clamp(r.x, -kMaxCoordinate, kMaxCoordinate);
  r.y = std::clamp(r.y, -kMaxCoordinate, kMaxCoordinate);
  r.w = std::clamp(r.w, config.minFloatSize, kMaxCoordinate);
  r.h = std::clamp(r.h, config.minFloatSize, kMaxCoordinate);
  return !(r == before);
}

bool readWindow(const JsonValue& area, ReadContext& ctx, WindowState& out, std::string& error) {
  const JsonValue* json = area.find("window");
  if (json == nullptr) return true;
  if (!json->isObject()) {
    error = "window state is not an object";
    return false;
  }
  error = checkMembers(*json, {"maximized", "monitor", "monitorIndex", "dpi", "rect"}, "window state");
  if (!error.empty()) return false;
  if (const JsonValue* v = json->find("maximized")) {
    if (v->type() != JsonType::Bool) {
      error = "window member \"maximized\" has the wrong type";
      return false;
    }
    out.maximized = v->boolValue();
  }
  if (const JsonValue* v = json->find("monitor")) {
    if (!v->isString()) {
      error = "window member \"monitor\" has the wrong type";
      return false;
    }
    out.monitor = detail::cleanText(v->stringValue(), kMaxNameBytes);
    if (out.monitor.size() < v->stringValue().size()) ctx.repair("a monitor name was too long and was cut");
  }
  if (const JsonValue* v = json->find("monitorIndex")) {
    if (v->type() != JsonType::Number) {
      error = "window member \"monitorIndex\" has the wrong type";
      return false;
    }
    const double index = v->numberValue();
    if (index >= -1.0 && index <= 1024.0 && index == std::floor(index)) {
      out.monitorIndex = static_cast<int>(index);
    } else {
      ctx.repair("a monitor index was out of range and was dropped");
    }
  }
  if (const JsonValue* v = json->find("dpi")) {
    if (v->type() != JsonType::Number) {
      error = "window member \"dpi\" has the wrong type";
      return false;
    }
    const double dpi = v->numberValue();
    if (dpi > 0.0 && dpi <= 64.0) {
      out.dpiScale = dpi;
    } else {
      ctx.repair("a display scale was out of range and was reset to 1");
    }
  }
  if (const JsonValue* v = json->find("rect")) {
    if (v->type() != JsonType::Object) {
      error = "window member \"rect\" has the wrong type";
      return false;
    }
    Rect rect;
    if (!readRectObject(*v, rect, error)) return false;
    if (rectInRange(rect)) {
      out.hasRect = true;
      out.rect = rect;
    } else {
      ctx.repair("a window rectangle was out of range and was dropped");
    }
  }
  return true;
}

const char* const kSideNames[] = {"left", "right", "top", "bottom"};

// One closed-panel record. A damaged record is dropped with a warning, never fatal.
bool readClosedSlot(const JsonValue& json, ReadContext& ctx, ClosedSlot& out) {
  if (!json.isObject()) return false;
  if (!checkMembers(json, {"panel", "neighbours", "index", "anchor", "side", "floating", "rect"}, "closed slot").empty()) {
    return false;
  }
  std::string ignored;
  double panel = 0.0, index = 0.0, anchor = 0.0;
  const JsonValue* p = member(json, "panel", JsonType::Number, ignored, "slot");
  const JsonValue* nb = member(json, "neighbours", JsonType::Array, ignored, "slot");
  const JsonValue* ix = member(json, "index", JsonType::Number, ignored, "slot");
  const JsonValue* an = member(json, "anchor", JsonType::Number, ignored, "slot");
  const JsonValue* sd = member(json, "side", JsonType::String, ignored, "slot");
  const JsonValue* fl = member(json, "floating", JsonType::Bool, ignored, "slot");
  if (!p || !nb || !ix || !an || !sd || !fl) return false;
  if (!readIndex(*p, static_cast<double>(UINT32_MAX), panel) || !readIndex(*ix, static_cast<double>(kMaxPanels), index) ||
      !readIndex(*an, static_cast<double>(UINT32_MAX), anchor)) {
    return false;
  }
  if (nb->size() > kMaxPanels) return false;
  bool sideOk = false;
  for (size_t i = 0; i < 4; ++i) {
    if (sd->stringValue() == kSideNames[i]) {
      out.anchorSide = static_cast<Side>(i);
      sideOk = true;
    }
  }
  if (!sideOk) return false;
  out.panel = static_cast<PanelId>(panel);
  out.index = static_cast<size_t>(index);
  out.anchor = static_cast<PanelId>(anchor);
  out.floating = fl->boolValue();
  if (ctx.known.count(out.panel) == 0) return false;
  if (out.anchor != 0 && (ctx.known.count(out.anchor) == 0 || out.anchor == out.panel)) out.anchor = 0;
  for (size_t i = 0; i < nb->size(); ++i) {
    double id = 0.0;
    if (!readIndex(nb->child(i), static_cast<double>(UINT32_MAX), id)) return false;
    const PanelId n = static_cast<PanelId>(id);
    if (ctx.known.count(n) != 0 && n != out.panel &&
        std::find(out.neighbours.begin(), out.neighbours.end(), n) == out.neighbours.end()) {
      out.neighbours.push_back(n);
    }
  }
  if (out.floating) {
    if (!readRect(json, out.floatRect, ignored, "slot") || !rectInRange(out.floatRect)) return false;
  }
  return true;
}

// ---- panels list (version 2) ---------------------------------------------------------------------

struct PanelListing {
  std::unordered_set<PanelId> listed;
  std::vector<std::pair<PanelId, bool>> locks;  // known panels with their stored lock flag
};

bool readPanelList(const JsonValue& list, ReadContext& ctx, PanelListing& out, std::string& error) {
  if (list.size() > kMaxPanels) {
    error = "layout lists more panels than the limit";
    return false;
  }
  for (size_t i = 0; i < list.size(); ++i) {
    const JsonValue& entry = list.child(i);
    if (!entry.isObject()) {
      error = "panel entry is not an object";
      return false;
    }
    error = checkMembers(entry, {"id", "locked"}, "panel entry");
    if (!error.empty()) return false;
    const JsonValue* id = member(entry, "id", JsonType::Number, error, "panel entry");
    if (id == nullptr) return false;
    bool locked = false;
    if (const JsonValue* l = entry.find("locked")) {
      if (l->type() != JsonType::Bool) {
        error = "panel entry member \"locked\" has the wrong type";
        return false;
      }
      locked = l->boolValue();
    }
    double value = 0.0;
    if (!readIndex(*id, static_cast<double>(UINT32_MAX), value)) {
      ctx.repair("a panel entry with an invalid id was dropped");
      continue;
    }
    const PanelId panel = static_cast<PanelId>(value);
    if (!out.listed.insert(panel).second) {
      ctx.warn("panel " + std::to_string(panel) + " is listed twice; the later entry was ignored");
      continue;
    }
    if (ctx.known.count(panel) != 0) out.locks.emplace_back(panel, locked);
  }
  return true;
}

}  // namespace

LoadResult DockLayout::fromJson(std::string_view text, std::vector<PanelInfo> panels, const DockConfig& config,
                                const LoadOptions& options) {
  LoadResult result;
  const auto fail = [&](std::string message) {
    result.layout.reset();
    result.error = std::move(message);
    return std::move(result);
  };

  core::JsonLimits limits;
  limits.maxDepth = 96;  // two JSON levels per tree level plus the wrapper objects
  limits.maxInputBytes = size_t{4} * 1024 * 1024;
  limits.maxNodes = 200000;
  const core::JsonResult parsed = core::parseJson(text, limits);
  if (!parsed.ok()) {
    return fail("layout is not valid JSON: " + parsed.error.message + " (offset " + std::to_string(parsed.error.offset) + ")");
  }
  const JsonValue& doc = *parsed.value;
  std::string error;
  if (!doc.isObject()) return fail("layout is not a JSON object");
  const JsonValue* version = member(doc, "version", JsonType::Number, error, "layout");
  if (version == nullptr) return fail(error);
  const double v = version->numberValue();
  if (v != std::floor(v) || v < 1.0) return fail("layout version is not a valid version number");
  if (v > static_cast<double>(kReadableVersions)) {
    return fail("unsupported layout version " + std::to_string(static_cast<long long>(std::min(v, 1.0e9))) +
                " (this build reads versions 1 and " + std::to_string(kReadableVersions) + ")");
  }
  result.sourceVersion = static_cast<int>(v);
  const bool strict = result.sourceVersion == 1;
  error = strict ? checkMembers(doc, {"version", "main", "floating"}, "layout")
                 : checkMembers(doc, {"version", "name", "description", "main", "floating", "panels", "closed"}, "layout");
  if (!error.empty()) return fail(error);
  const JsonValue* main = member(doc, "main", JsonType::Object, error, "layout");
  const JsonValue* floating = main != nullptr ? member(doc, "floating", JsonType::Array, error, "layout") : nullptr;
  if (main == nullptr || floating == nullptr) return fail(error);
  error = strict ? checkMembers(*main, {"root"}, "main area") : checkMembers(*main, {"root", "window"}, "main area");
  if (!error.empty()) return fail(error);
  if (floating->size() > kMaxFloatingAreas) return fail("layout has too many floating areas");

  std::unordered_set<PanelId> known;
  for (const PanelInfo& p : panels) known.insert(p.id);
  ReadContext ctx{known, strict, {}, 0, 0, 0, 0, result.warnings};

  LayoutMeta meta;
  if (!strict) {
    for (const char* key : {"name", "description"}) {
      const JsonValue* s = doc.find(key);
      if (s == nullptr) continue;
      if (!s->isString()) return fail(std::string("layout member \"") + key + "\" has the wrong type");
      const size_t cap = std::string_view(key) == "name" ? kMaxNameBytes : kMaxDescriptionBytes;
      std::string cleaned = detail::cleanText(s->stringValue(), cap);
      if (cleaned.size() < s->stringValue().size()) ctx.repair(std::string("the layout ") + key + " was too long and was cut");
      (std::string_view(key) == "name" ? meta.name : meta.description) = std::move(cleaned);
    }
  }

  std::vector<Area> areas;
  Area mainArea;
  mainArea.id = kMainAreaId;
  if (!readRoot(*main, ctx, mainArea.root, error)) return fail(error);
  if (!strict && !readWindow(*main, ctx, mainArea.window, error)) return fail(error);
  areas.push_back(std::move(mainArea));
  for (size_t i = 0; i < floating->size(); ++i) {
    const JsonValue& json = floating->child(i);
    if (!json.isObject()) return fail("floating area is not an object");
    error = strict ? checkMembers(json, {"rect", "root"}, "floating area") : checkMembers(json, {"rect", "root", "window"}, "floating area");
    if (!error.empty()) return fail(error);
    Area area;
    area.id = core::checkedCast<uint32_t>(i + 1);
    if (!readRect(json, area.rect, error, "floating area") || !readRoot(json, ctx, area.root, error)) return fail(error);
    if (!strict && !readWindow(json, ctx, area.window, error)) return fail(error);
    if (!area.root) return fail("floating area has no root");
    if (!strict && repairFloatRect(area.rect, config)) ctx.repair("a floating window rectangle was out of range and was repaired");
    areas.push_back(std::move(area));
  }

  PanelListing listing;
  std::vector<ClosedSlot> closed;
  if (!strict) {
    if (const JsonValue* list = doc.find("panels")) {
      if (list->type() != JsonType::Array) return fail("layout member \"panels\" has the wrong type");
      if (!readPanelList(*list, ctx, listing, error)) return fail(error);
    }
    if (const JsonValue* list = doc.find("closed")) {
      if (list->type() != JsonType::Array) return fail("layout member \"closed\" has the wrong type");
      if (list->size() > kMaxPanels) return fail("layout has more closed-panel records than the limit");
      std::unordered_set<PanelId> slotted;
      for (size_t i = 0; i < list->size(); ++i) {
        ClosedSlot slot;
        if (!readClosedSlot(list->child(i), ctx, slot) || !slotted.insert(slot.panel).second) {
          ctx.repair("a damaged closed-panel record was dropped");
          continue;
        }
        closed.push_back(std::move(slot));
      }
    }
  }

  // Stored lock flags win over the registry's defaults (decision D13).
  for (const auto& [panel, locked] : listing.locks) {
    for (PanelInfo& p : panels) {
      if (p.id == panel) p.locked = locked;
    }
  }

  DockLayoutResult created = DockLayout::create(std::move(panels), config);
  if (!created.ok()) return fail(created.error);
  DockLayout layout = std::move(*created.layout);
  Status status = layout.commitWith(std::move(areas), core::checkedCast<uint32_t>(floating->size() + 1), std::move(closed));
  if (!status) return fail(status.error);
  const bool anyDocked = std::any_of(layout.panels_.begin(), layout.panels_.end(),
                                     [&](const PanelInfo& p) { return layout.isDocked(p.id); });
  if (!anyDocked) return fail("layout has no usable panels");
  if (!strict) layout.meta_ = std::move(meta);

  // Panels the stored layout never mentioned appear at the position their module suggests; the
  // others stay closed until the user opens them (spec 04 rules 42-43).
  if (!strict && options.placeNewPanels && !listing.listed.empty()) {
    const std::vector<PanelInfo> hostPanels = layout.panels_;
    for (const PanelInfo& p : hostPanels) {
      if (listing.listed.count(p.id) != 0 || layout.isDocked(p.id) || p.suggested.relativeTo == 0) continue;
      if (!layout.isDocked(p.suggested.relativeTo)) continue;
      if (layout.openPanel(p.id).status) ++result.newPanelsPlaced;
    }
  }
  if (options.monitors != nullptr) result.windowsMoved = layout.fitWindows(*options.monitors);
  result.droppedPanels = ctx.dropped;
  result.duplicatePanels = ctx.duplicates;
  result.repairedValues = ctx.repaired;
  result.layout = std::move(layout);
  return result;
}

}  // namespace r1ui::dock
