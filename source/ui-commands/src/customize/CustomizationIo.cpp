// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the JSON export and strict parser of Delta (CustomizationIo.h) and the load, save, import and
//   storage glue around the stores.
// Invariants: parseDelta never throws and never returns a Delta that effectiveLayout could not
//   handle (it could anyway: effectiveLayout is total); every entry is validated field by field before
//   it is kept; ids are unique across added nodes and user containers (the first wins); counts are
//   bounded before memory grows (file size, JSON nesting and node limits, kMaxNodes entries).
//   exportDelta output parses back to an equal Delta (a round trip test covers it).
// Callers: CustomizationStorage, hosts, tests.
#include "r1ui/commands/customize/CustomizationIo.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "r1ui/commands/Text.h"
#include "r1ui/core/Json.h"
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

// ---- parse --------------------------------------------------------------------------------------

namespace {

using core::JsonType;
using core::JsonValue;

class Parser {
 public:
  explicit Parser(ParseResult& result) : r_(result) {}

  void run(const JsonValue& root) {
    size_t total = 0;
    for (const char* name : {"edits", "moves", "added", "toolbars", "panels", "toolbarEdits", "panelEdits"}) {
      const JsonValue* v = root.find(name);
      if (v != nullptr && v->type() == JsonType::Array) total += v->size();
    }
    if (total > kMaxNodes) {
      r_.error = "too many entries (limit " + std::to_string(kMaxNodes) + ")";
      return;
    }
    if (const JsonValue* v = root.find("serial")) {
      if (v->type() == JsonType::Number && v->numberValue() >= 0.0 && v->numberValue() <= 4.0e9 && std::floor(v->numberValue()) == v->numberValue()) {
        r_.delta.serial = static_cast<uint32_t>(v->numberValue());
      } else {
        issue("serial", "not a counter; ignored");
      }
    }
    each(root, "edits", [&](const JsonValue& v, const std::string& at) { edit(v, at); });
    each(root, "moves", [&](const JsonValue& v, const std::string& at) { move(v, at); });
    each(root, "added", [&](const JsonValue& v, const std::string& at) { added(v, at); });
    each(root, "toolbars", [&](const JsonValue& v, const std::string& at) { toolbar(v, at); });
    each(root, "panels", [&](const JsonValue& v, const std::string& at) { panel(v, at); });
    each(root, "toolbarEdits", [&](const JsonValue& v, const std::string& at) { toolbarEdit(v, at); });
    each(root, "panelEdits", [&](const JsonValue& v, const std::string& at) { panelEdit(v, at); });
    if (total > 0 && r_.delta.empty()) r_.error = "the file has entries but none is usable";
  }

 private:
  void issue(const std::string& where, std::string message) {
    if (r_.issues.size() < 256) r_.issues.push_back({where, std::move(message)});
  }

  bool skip(const std::string& where, std::string message) {
    ++r_.skipped;
    issue(where, std::move(message));
    return false;
  }

  template <class Fn>
  void each(const JsonValue& root, const char* name, Fn fn) {
    const JsonValue* v = root.find(name);
    if (v == nullptr) return;
    if (v->type() != JsonType::Array) {
      issue(name, "not an array; ignored");
      return;
    }
    for (size_t i = 0; i < v->size(); ++i) {
      const std::string at = std::string(name) + "[" + std::to_string(i) + "]";
      if (!v->child(i).isObject()) {
        skip(at, "entry is not an object");
        continue;
      }
      fn(v->child(i), at);
    }
  }

  // Text: valid by construction (the JSON parser rejected invalid UTF-8); control characters become spaces
  // and over-long text is cut, both counted as a repair.
  std::string text(const JsonValue& obj, const char* name, const std::string& at, bool& repaired) {
    const JsonValue* v = obj.find(name);
    if (v == nullptr || !v->isString()) return {};
    std::string clean = sanitizeText(v->stringValue(), kMaxLabelBytes);
    if (clean != v->stringValue()) {
      repaired = true;
      issue(at, std::string("text of '") + name + "' was cleaned or shortened");
    }
    return clean;
  }

  std::optional<std::string> id(const JsonValue& obj, const char* name, const std::string& at) {
    const JsonValue* v = obj.find(name);
    if (v == nullptr || !v->isString() || !isValidIdentifier(v->stringValue(), kMaxIdBytes)) {
      skip(at, std::string("'") + name + "' is missing or not a valid id");
      return std::nullopt;
    }
    return v->stringValue();
  }

  std::string optionalId(const JsonValue& obj, const char* name) {
    const JsonValue* v = obj.find(name);
    return (v != nullptr && v->isString() && isValidIdentifier(v->stringValue(), kMaxIdBytes)) ? v->stringValue() : std::string();
  }

  std::optional<Rect> rect(const JsonValue& obj, const std::string& at) {
    const JsonValue* v = obj.find("rect");
    if (v == nullptr) return std::nullopt;
    if (v->type() != JsonType::Array || v->size() != 4) {
      skip(at, "rect must be [x, y, w, h]");
      return std::nullopt;
    }
    double n[4];
    for (size_t i = 0; i < 4; ++i) {
      if (v->child(i).type() != JsonType::Number) {
        skip(at, "rect must hold numbers");
        return std::nullopt;
      }
      n[i] = v->child(i).numberValue();
    }
    const Rect r{n[0], n[1], n[2], n[3]};
    if (!std::isfinite(r.x) || !std::isfinite(r.y) || std::fabs(r.x) > kMaxPanelSize || std::fabs(r.y) > kMaxPanelSize || r.w < kMinButtonSize ||
        r.h < kMinButtonSize || r.w > kMaxPanelSize || r.h > kMaxPanelSize) {
      skip(at, "rect is out of range (size at least " + std::to_string(static_cast<int>(kMinButtonSize)) + ")");
      return std::nullopt;
    }
    return r;
  }

  Side side(const JsonValue& obj, const std::string& at) {
    Side s = Side::End;
    const JsonValue* v = obj.find("side");
    if (v != nullptr && (!v->isString() || !parseSide(v->stringValue(), s))) {
      issue(at, "unknown side; 'end' used");
      ++r_.repaired;
      s = Side::End;
    }
    return s;
  }

  bool claim(const std::string& value, const std::string& at) {
    if (!ids_.insert(value).second) return skip(at, "duplicate id '" + value + "'; the first one is kept");
    return true;
  }

  void edit(const JsonValue& v, const std::string& at) {
    const auto node = id(v, "node", at);
    if (!node) return;
    if (r_.delta.edits.count(*node) != 0) {
      skip(at, "second edit of '" + *node + "'; the first one is kept");
      return;
    }
    NodeEdit e;
    bool repaired = false;
    if (const JsonValue* h = v.find("hidden")) {
      if (h->type() == JsonType::Bool) {
        e.hidden = h->boolValue();
      } else {
        issue(at, "hidden is not a boolean; ignored");
      }
    }
    if (const JsonValue* l = v.find("label")) {
      if (l->isString()) e.label = text(v, "label", at, repaired);
    }
    const size_t before = r_.skipped;
    if (const auto r = rect(v, at)) e.rect = r;
    if (r_.skipped != before) return;  // a bad rectangle rejects the entry
    if (e.empty()) {
      skip(at, "edit changes nothing");
      return;
    }
    if (repaired) ++r_.repaired;
    r_.delta.edits.emplace(*node, std::move(e));
  }

  void move(const JsonValue& v, const std::string& at) {
    const auto node = id(v, "node", at);
    const auto parent = node ? id(v, "parent", at) : std::nullopt;
    if (!node || !parent) return;
    const auto duplicate = [&](const MoveEdit& m) { return m.node == *node; };
    if (std::any_of(r_.delta.moves.begin(), r_.delta.moves.end(), duplicate)) {
      skip(at, "second move of '" + *node + "'; the first one is kept");
      return;
    }
    r_.delta.moves.push_back({*node, {*parent, optionalId(v, "anchor"), side(v, at)}});
  }

  void added(const JsonValue& v, const std::string& at) {
    const auto nodeId = id(v, "id", at);
    if (!nodeId) return;
    const JsonValue* k = v.find("kind");
    Kind kind = Kind::Command;
    if (k == nullptr || !k->isString() || !parseNodeKind(k->stringValue(), kind)) {
      skip(at, "unknown kind");
      return;
    }
    const auto parent = id(v, "parent", at);
    if (!parent) return;
    Node n;
    n.id = *nodeId;
    n.kind = kind;
    n.user = true;
    bool repaired = false;
    n.label = text(v, "label", at, repaired);
    if ((kind == Kind::Command || kind == Kind::FreeButton)) {
      const auto command = id(v, "command", at);
      if (!command) return;
      n.commandId = *command;
    }
    if (const JsonValue* h = v.find("hidden")) n.visible = !(h->type() == JsonType::Bool && h->boolValue());
    if (kind == Kind::FreeButton) {
      const auto r = rect(v, at);
      if (!r) {
        if (v.find("rect") == nullptr) skip(at, "a free-form button needs a rect");
        return;
      }
      n.rect = *r;
    }
    if (!claim(*nodeId, at)) return;
    if (repaired) ++r_.repaired;
    r_.delta.added.push_back({std::move(n), {*parent, optionalId(v, "anchor"), side(v, at)}});
  }

  void toolbar(const JsonValue& v, const std::string& at) {
    const auto tid = id(v, "id", at);
    if (!tid) return;
    ToolbarLayout t;
    t.id = *tid;
    t.user = true;
    bool repaired = false;
    t.title = text(v, "title", at, repaired);
    const JsonValue* o = v.find("orientation");
    if (o != nullptr && o->isString() && o->stringValue() == "vertical") t.orientation = Orientation::Vertical;
    if (const JsonValue* s = v.find("sizeStep")) {
      if (!s->isString() || !parseSizeStep(s->stringValue(), t.sizeStep)) issue(at, "unknown sizeStep; 'medium' used");
    }
    if (const JsonValue* g = v.find("gap")) {
      if (g->type() == JsonType::Number && g->numberValue() >= kMinToolbarGap && g->numberValue() <= kMaxToolbarGap) {
        t.gap = g->numberValue();
      } else {
        issue(at, "gap out of range; default used");
        ++r_.repaired;
      }
    }
    if (!claim(*tid, at)) return;
    if (repaired) ++r_.repaired;
    r_.delta.userToolbars.push_back(std::move(t));
  }

  void panel(const JsonValue& v, const std::string& at) {
    const auto pid = id(v, "id", at);
    if (!pid) return;
    FreeFormPanelLayout p;
    p.id = *pid;
    p.user = true;
    bool repaired = false;
    p.title = text(v, "title", at, repaired);
    const JsonValue* w = v.find("width");
    const JsonValue* h = v.find("height");
    const auto okSize = [](const JsonValue* s) {
      return s != nullptr && s->type() == JsonType::Number && s->numberValue() >= kMinButtonSize * 2.0 && s->numberValue() <= kMaxPanelSize;
    };
    if (!okSize(w) || !okSize(h)) {
      skip(at, "panel size is missing or out of range");
      return;
    }
    p.width = w->numberValue();
    p.height = h->numberValue();
    if (const JsonValue* s = v.find("snap")) p.snap = s->type() == JsonType::Bool && s->boolValue();
    if (const JsonValue* g = v.find("grid")) {
      if (g->type() == JsonType::Number && g->numberValue() >= 1.0 && g->numberValue() <= 256.0) {
        p.grid = g->numberValue();
      } else {
        issue(at, "grid out of range; default used");
        ++r_.repaired;
      }
    }
    if (!claim(*pid, at)) return;
    if (repaired) ++r_.repaired;
    r_.delta.userPanels.push_back(std::move(p));
  }

  void toolbarEdit(const JsonValue& v, const std::string& at) {
    const auto tid = id(v, "id", at);
    if (!tid) return;
    if (r_.delta.toolbarEdits.count(*tid) != 0) {
      skip(at, "second settings entry for '" + *tid + "'; the first one is kept");
      return;
    }
    ToolbarEdit e;
    if (const JsonValue* s = v.find("sizeStep")) {
      SizeStep step = SizeStep::Medium;
      if (s->isString() && parseSizeStep(s->stringValue(), step)) {
        e.sizeStep = step;
      } else {
        issue(at, "unknown sizeStep; ignored");
      }
    }
    if (const JsonValue* g = v.find("gap")) {
      if (g->type() == JsonType::Number && g->numberValue() >= kMinToolbarGap && g->numberValue() <= kMaxToolbarGap) {
        e.gap = g->numberValue();
      } else {
        issue(at, "gap out of range; ignored");
      }
    }
    if (!e.sizeStep && !e.gap) {
      skip(at, "settings change nothing");
      return;
    }
    r_.delta.toolbarEdits.emplace(*tid, e);
  }

  void panelEdit(const JsonValue& v, const std::string& at) {
    const auto pid = id(v, "id", at);
    if (!pid) return;
    if (r_.delta.panelEdits.count(*pid) != 0) {
      skip(at, "second settings entry for '" + *pid + "'; the first one is kept");
      return;
    }
    PanelEdit e;
    if (const JsonValue* s = v.find("snap")) {
      if (s->type() == JsonType::Bool) e.snap = s->boolValue();
    }
    if (const JsonValue* g = v.find("grid")) {
      if (g->type() == JsonType::Number && g->numberValue() >= 1.0 && g->numberValue() <= 256.0) {
        e.grid = g->numberValue();
      } else {
        issue(at, "grid out of range; ignored");
      }
    }
    if (!e.snap && !e.grid) {
      skip(at, "settings change nothing");
      return;
    }
    r_.delta.panelEdits.emplace(*pid, e);
  }

  ParseResult& r_;
  std::unordered_set<std::string> ids_;
};

}  // namespace

ParseResult parseDelta(std::string_view json) {
  ParseResult result;
  if (json.size() > kMaxFileBytes) {
    result.error = "customization file is larger than the " + std::to_string(kMaxFileBytes) + " byte limit";
    return result;
  }
  core::JsonLimits limits;
  limits.maxDepth = 8;
  limits.maxInputBytes = kMaxFileBytes;
  limits.maxNodes = 40 * kMaxNodes;
  const core::JsonResult parsed = core::parseJson(json, limits);
  if (!parsed.ok()) {
    result.error = "not valid JSON: " + parsed.error.message;
    return result;
  }
  const JsonValue& root = *parsed.value;
  if (!root.isObject()) {
    result.error = "the file must contain a JSON object";
    return result;
  }
  const JsonValue* format = root.find("format");
  if (format == nullptr || !format->isString() || format->stringValue() != kFormatName) {
    result.error = "not a customization file";
    return result;
  }
  const JsonValue* version = root.find("version");
  if (version == nullptr || version->type() != JsonType::Number || version->numberValue() != kFormatVersion) {
    result.error = version != nullptr && version->type() == JsonType::Number && version->numberValue() > kFormatVersion
                       ? "the file was written by a newer version"
                       : "unsupported customization file version";
    return result;
  }
  Parser(result).run(root);
  result.ok = result.error.empty();
  if (!result.ok) result.delta = Delta{};
  return result;
}

// ---- load, save, import -------------------------------------------------------------------------

namespace {

void note(LoadReport& report, const std::string& source, const ParseResult& parsed) {
  for (const ParseIssue& i : parsed.issues) report.issues.push_back({source + ": " + i.where, i.message});
}

// Reads one layer; returns the parsed delta when the layer was acceptable.
std::optional<Delta> readLayer(TextStore& store, const char* name, LoadReport& report, std::string& keptAside) {
  const TextLoad loaded = store.load();
  if (!loaded.error.empty()) {
    report.ok = false;
    if (report.error.empty()) report.error = std::string(name) + ": " + loaded.error;
    if (loaded.exists) {
      std::string moveError;
      keptAside = store.setAside(moveError);
    }
    return std::nullopt;
  }
  if (!loaded.exists) return std::nullopt;
  ParseResult parsed = parseDelta(loaded.text);
  note(report, name, parsed);
  if (!parsed.ok) {
    report.ok = false;
    if (report.error.empty()) report.error = std::string(name) + " file rejected: " + parsed.error;
    std::string moveError;
    keptAside = store.setAside(moveError);
    return std::nullopt;
  }
  return std::move(parsed.delta);
}

}  // namespace

LoadReport loadCustomization(Customization& customization, TextStore& user, TextStore* workspace) {
  LoadReport report;
  if (auto delta = readLayer(user, "user", report, report.userKeptAside)) {
    customization.setUserDelta(std::move(*delta));
    report.userLoaded = true;
  }
  if (workspace != nullptr) {
    if (auto delta = readLayer(*workspace, "workspace", report, report.workspaceKeptAside)) {
      customization.setWorkspaceDelta(std::move(*delta));
      report.workspaceLoaded = true;
    }
  }
  return report;
}

bool saveCustomization(const Customization& customization, TextStore& user, std::string& error) {
  return user.save(exportDelta(customization.userDelta()), error);
}

LoadReport importCustomization(Customization& customization, std::string_view json, ImportMode mode) {
  LoadReport report;
  ParseResult parsed = parseDelta(json);
  note(report, "import", parsed);
  if (!parsed.ok) {
    report.ok = false;
    report.error = "import rejected: " + parsed.error;
    return report;
  }
  if (mode == ImportMode::Merge) {
    customization.setUserDelta(mergeDeltas(customization.userDelta(), parsed.delta));
  } else {
    customization.setUserDelta(std::move(parsed.delta));
  }
  report.userLoaded = true;
  return report;
}

std::string exportCustomization(const Customization& customization) { return exportDelta(customization.userDelta()); }

CustomizationStorage::CustomizationStorage(Customization& customization, TextStore& user, TextStore* workspace)
    : customization_(customization), user_(user), workspace_(workspace) {
  customization_.setOnCommit([this] { save(); });
}

CustomizationStorage::~CustomizationStorage() { customization_.setOnCommit({}); }

LoadReport CustomizationStorage::load() {
  LoadReport report = loadCustomization(customization_, user_, workspace_);
  lastError_ = report.error;
  return report;
}

bool CustomizationStorage::save() {
  std::string error;
  const bool ok = saveCustomization(customization_, user_, error);
  lastError_ = ok ? std::string() : error;
  return ok;
}

}  // namespace r1ui::commands::customize
