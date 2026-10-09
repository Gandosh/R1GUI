// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: LayoutManager (see LayoutManager.h for the contract): startup and fallback, applying
//   layouts, named layout management, reset, import and export.
// Invariants: nothing is written to the store before the layout it holds has been validated; the
//   target is only changed through ILayoutTarget::applyLayout with a layout that already parsed;
//   a damaged "_active" layout is preserved (renamed or copied) before the default replaces it, and
//   if it could not be preserved auto-save stays off until the user explicitly loads or resets.
// Callers: the shell and tests. Auto-save lives in LayoutManagerAutosave.cpp.
#include "r1ui/dock/LayoutManager.h"

#include <algorithm>
#include <cctype>

#include "r1ui/core/Json.h"

namespace r1ui::dock {

namespace {

// Name and description of a stored layout without validating the rest of the file.
LayoutMeta readMetaOnly(const std::string& text) {
  LayoutMeta meta;
  if (text.size() > size_t{1} << 20) return meta;
  core::JsonLimits limits;
  limits.maxDepth = 96;
  const core::JsonResult parsed = core::parseJson(text, limits);
  if (!parsed.ok() || !parsed.value->isObject()) return meta;
  if (const core::JsonValue* name = parsed.value->find("name"); name != nullptr && name->isString()) {
    meta.name = name->stringValue().substr(0, kMaxNameBytes);
  }
  if (const core::JsonValue* d = parsed.value->find("description"); d != nullptr && d->isString()) {
    meta.description = d->stringValue().substr(0, kMaxDescriptionBytes);
  }
  return meta;
}

std::string trimmed(std::string_view text) {
  size_t b = 0, e = text.size();
  while (b < e && std::isspace(static_cast<unsigned char>(text[b])) != 0) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1])) != 0) --e;
  return std::string(text.substr(b, e - b));
}

std::string cleaned(std::string_view text, size_t cap) {
  std::string out(text.substr(0, std::min(text.size(), cap)));
  while (!out.empty() && out.size() < text.size() && (static_cast<unsigned char>(text[out.size()]) & 0xC0) == 0x80) out.pop_back();
  for (char& c : out) {
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = ' ';
  }
  return out;
}

bool userKey(std::string_view key) { return keyProblem(key).empty() && key.front() != '_'; }

}  // namespace

LayoutManager::LayoutManager(ILayoutStore& store, IClock& clock, ILayoutTarget& target, LayoutManagerOptions options)
    : store_(store), clock_(clock), target_(target), options_(options) {}

// ---- helpers -------------------------------------------------------------------------------------

std::string LayoutManager::readableName(std::string_view key) const {
  std::string out;
  for (size_t i = 0; i < key.size(); ++i) {
    const char c = key[i];
    if (c == '_' || c == '-') {
      out.push_back(' ');
      continue;
    }
    if (i > 0 && std::isupper(static_cast<unsigned char>(c)) != 0 && std::islower(static_cast<unsigned char>(key[i - 1])) != 0) {
      out.push_back(' ');
    }
    out.push_back(c);
  }
  return out;
}

// A display name may be any text (a name in another script has no file-name characters), so the
// key falls back to "layout-N" when nothing of the name survives.
std::string LayoutManager::newKeyFor(std::string_view display) const {
  std::string key = keyFromName(display);
  if (!key.empty()) return key;
  for (int n = 1; n < 10000; ++n) {
    key = "layout-" + std::to_string(n);
    if (!store_.exists(mode_, key)) return key;
  }
  return {};
}

std::string LayoutManager::keptAsideKey() const {
  for (int n = 1; n < 99; ++n) {
    const std::string key = "_corrupt-" + std::to_string(n);
    if (!store_.exists(mode_, key)) return key;
  }
  return "_corrupt-99";
}

LayoutReport LayoutManager::reportFrom(const LoadResult& loaded, std::string key) const {
  LayoutReport report;
  report.ok = loaded.ok();
  report.error = loaded.error;
  report.key = std::move(key);
  report.droppedPanels = loaded.droppedPanels;
  report.duplicatePanels = loaded.duplicatePanels;
  report.repairedValues = loaded.repairedValues;
  report.windowsMoved = loaded.windowsMoved;
  report.newPanelsPlaced = loaded.newPanelsPlaced;
  report.warnings = loaded.warnings;
  if (report.warnings.size() > options_.maxWarnings) report.warnings.resize(options_.maxWarnings);
  return report;
}

std::optional<LoadResult> LayoutManager::parse(std::string_view text, std::string& error) const {
  MonitorSet monitors;
  LoadOptions options;
  if (monitorProvider_) {
    monitors = monitorProvider_();
    options.monitors = &monitors;
  }
  LoadResult loaded = DockLayout::fromJson(text, target_.panels(), target_.config(), options);
  if (!loaded.ok()) {
    error = loaded.error;
    return std::nullopt;
  }
  return loaded;
}

// Applies an already validated layout. Auto-save is held off while the target rebuilds (its change
// notifications are part of the switch, spec 04 rule 11), then the new arrangement is written as
// the active layout at once.
LayoutReport LayoutManager::applyLoaded(LoadResult loaded, std::string key) {
  LayoutReport report = reportFrom(loaded, key);
  const bool wasSuspended = suspended_;
  suspended_ = true;
  const Status applied = target_.applyLayout(std::move(*loaded.layout));
  suspended_ = wasSuspended;
  if (!applied) {
    report.ok = false;
    report.error = applied.error;
    return report;
  }
  activeKey_ = std::move(key);
  changedWhileSuspended_ = false;
  dirty_ = true;  // writeActive clears it on success
  if (const Status written = writeActive(); !written) report.warnings.push_back("the layout was applied but could not be saved: " + written.error);
  return report;
}

Status LayoutManager::storeCurrentAs(std::string_view key, std::string_view name, std::string_view description) {
  DockLayout copy = target_.currentLayout();
  if (const Status s = copy.setMeta({std::string(name), std::string(description)}); !s) return s;
  return store_.write(mode_, key, copy.toJson());
}

// ---- mode ---------------------------------------------------------------------------------------

Status LayoutManager::setMode(std::string mode) {
  if (std::string problem = keyProblem(mode); !problem.empty()) return Status::failure("application mode: " + problem);
  if (mode.front() == '_') return Status::failure("application mode cannot start with an underscore");
  if (const Status saved = flush(); !saved) return Status::failure("could not save the layout of mode \"" + mode_ + "\": " + saved.error);
  mode_ = std::move(mode);
  activeKey_.clear();
  dirty_ = false;
  changedWhileSuspended_ = false;
  autosaveBlocked_ = false;
  return Status::success();
}

// ---- startup ------------------------------------------------------------------------------------

LayoutReport LayoutManager::startup() {
  LayoutReport report;
  std::string reason;
  if (const std::optional<std::string> text = store_.read(mode_, kActiveKey)) {
    std::optional<LoadResult> loaded = parse(*text, reason);
    if (loaded) {
      LayoutReport applied = applyLoaded(std::move(*loaded), {});
      if (applied.ok) return applied;
      reason = applied.error;
    }
    // Spec 04 rules 14 and 44 and edge case 5: a layout that cannot be used is kept, not deleted.
    const std::string aside = keptAsideKey();
    Status preserved = store_.rename(mode_, kActiveKey, aside);
    if (!preserved) preserved = store_.write(mode_, aside, *text);
    if (preserved) {
      report.keptAsideAs = aside;
    } else {
      autosaveBlocked_ = true;
      lastError_ = "the damaged layout could not be kept aside; auto-save is off until a layout is loaded";
      report.warnings.push_back(lastError_);
    }
    report.warnings.push_back("the stored layout was not used: " + reason);
  }
  if (!defaultProvider_) {
    report.error = "no stored layout and no default layout";
    return report;
  }
  DockLayout fallback = defaultProvider_();
  LoadResult wrapped;
  wrapped.layout = std::move(fallback);
  if (monitorProvider_) {
    const MonitorSet monitors = monitorProvider_();
    wrapped.windowsMoved = wrapped.layout->fitWindows(monitors);
  }
  LayoutReport applied = applyLoaded(std::move(wrapped), {});
  applied.usedDefault = true;
  applied.keptAsideAs = report.keptAsideAs;
  applied.warnings.insert(applied.warnings.begin(), report.warnings.begin(), report.warnings.end());
  return applied;
}

// ---- named layouts ------------------------------------------------------------------------------

std::vector<LayoutSummary> LayoutManager::list() const {
  std::vector<LayoutSummary> out;
  for (const std::string& key : store_.list(mode_)) {
    if (key.empty() || key.front() == '_') continue;
    LayoutSummary item;
    item.key = key;
    LayoutMeta meta;
    if (const std::optional<std::string> text = store_.read(mode_, key)) meta = readMetaOnly(*text);
    item.displayName = meta.name.empty() ? readableName(key) : meta.name;
    item.description = meta.description;
    item.active = key == activeKey_;
    out.push_back(std::move(item));
  }
  return out;
}

Status LayoutManager::save(std::string_view key) {
  if (!userKey(key)) return Status::failure("not a usable layout name");
  const std::optional<std::string> old = store_.read(mode_, key);
  if (!old) return Status::failure("no layout named \"" + std::string(key) + "\" to overwrite");
  const LayoutMeta meta = readMetaOnly(*old);
  const Status written = storeCurrentAs(key, meta.name.empty() ? readableName(key) : meta.name, meta.description);
  if (!written) return written;
  activeKey_ = std::string(key);
  return flush(true);
}

Status LayoutManager::saveAs(std::string_view name, std::string_view description, std::string* keyOut) {
  const std::string display = cleaned(trimmed(name), kMaxNameBytes);
  if (display.empty()) return Status::failure("the name is empty");
  const std::string key = newKeyFor(display);
  if (key.empty()) return Status::failure("too many layouts with names that cannot be used in file names");
  if (store_.exists(mode_, key)) return Status::failure("a layout named \"" + key + "\" already exists; save over it instead");
  const Status written = storeCurrentAs(key, display, cleaned(description, kMaxDescriptionBytes));
  if (!written) return written;
  activeKey_ = key;
  if (keyOut != nullptr) *keyOut = key;
  return flush(true);
}

Status LayoutManager::rename(std::string_view key, std::string_view newName, std::string* keyOut) {
  if (!userKey(key)) return Status::failure("not a usable layout name");
  const std::string display = cleaned(trimmed(newName), kMaxNameBytes);
  const std::string newKey = display.empty() ? std::string() : newKeyFor(display);
  if (newKey.empty()) return Status::failure("the new name cannot be used");
  const std::optional<std::string> text = store_.read(mode_, key);
  if (!text) return Status::failure("no such layout");
  if (newKey != key && store_.exists(mode_, newKey)) return Status::failure("a layout with that name already exists");
  std::string error;
  std::optional<LoadResult> loaded = parse(*text, error);
  if (!loaded) return Status::failure("the stored layout is damaged: " + error);
  if (const Status s = loaded->layout->setMeta({display, readMetaOnly(*text).description}); !s) return s;
  if (const Status s = store_.write(mode_, newKey, loaded->layout->toJson()); !s) return s;
  if (newKey != key) {
    if (const Status s = store_.remove(mode_, key); !s) return s;
    if (activeKey_ == key) activeKey_ = newKey;
  }
  if (keyOut != nullptr) *keyOut = newKey;
  return Status::success();
}

Status LayoutManager::duplicate(std::string_view key, std::string_view newName, bool keepDescription, std::string* keyOut) {
  if (!userKey(key)) return Status::failure("not a usable layout name");
  const std::string display = cleaned(trimmed(newName), kMaxNameBytes);
  const std::string newKey = display.empty() ? std::string() : newKeyFor(display);
  if (newKey.empty()) return Status::failure("the new name cannot be used");
  const std::optional<std::string> text = store_.read(mode_, key);
  if (!text) return Status::failure("no such layout");
  if (store_.exists(mode_, newKey)) return Status::failure("a layout with that name already exists");
  std::string error;
  std::optional<LoadResult> loaded = parse(*text, error);
  if (!loaded) return Status::failure("the stored layout is damaged: " + error);
  const std::string description = keepDescription ? readMetaOnly(*text).description : std::string();
  if (const Status s = loaded->layout->setMeta({display, description}); !s) return s;
  if (const Status s = store_.write(mode_, newKey, loaded->layout->toJson()); !s) return s;
  if (keyOut != nullptr) *keyOut = newKey;
  return Status::success();
}

Status LayoutManager::remove(std::string_view key) {
  if (!userKey(key)) return Status::failure("not a usable layout name");
  if (const Status s = store_.remove(mode_, key); !s) return s;
  if (activeKey_ == key) activeKey_.clear();
  return Status::success();
}

LayoutReport LayoutManager::load(std::string_view key) {
  LayoutReport report;
  report.key = std::string(key);
  if (!userKey(key)) {
    report.error = "not a usable layout name";
    return report;
  }
  const std::optional<std::string> text = store_.read(mode_, key);
  if (!text) {
    report.error = "no layout named \"" + std::string(key) + "\"";
    return report;
  }
  std::optional<LoadResult> loaded = parse(*text, report.error);
  if (!loaded) return report;
  autosaveBlocked_ = false;  // an explicit choice of layout replaces whatever was protected
  return applyLoaded(std::move(*loaded), std::string(key));
}

// ---- reset --------------------------------------------------------------------------------------

LayoutManager::ResetOutcome LayoutManager::resetToDefault(const std::function<bool(std::string_view)>& confirm) {
  ResetOutcome outcome;
  if (!defaultProvider_) {
    outcome.error = "no default layout is available";
    return outcome;
  }
  const bool yes = confirm && confirm("Reset the layout to the default arrangement? All windows and panels will be rearranged.");
  if (!yes) {
    outcome.cancelled = true;
    return outcome;
  }
  LoadResult wrapped;
  wrapped.layout = defaultProvider_();
  if (monitorProvider_) wrapped.windowsMoved = wrapped.layout->fitWindows(monitorProvider_());
  autosaveBlocked_ = false;
  const LayoutReport report = applyLoaded(std::move(wrapped), {});
  outcome.done = report.ok;
  outcome.error = report.error;
  return outcome;
}

// ---- import and export -----------------------------------------------------------------------------

LayoutReport LayoutManager::importText(std::string_view name, std::string_view text, bool overwrite) {
  LayoutReport report;
  if (text.size() > kMaxLayoutBytes) {
    report.error = "the file is larger than a layout can be";
    return report;
  }
  const std::string key = userKey(name) ? std::string(name) : keyFromName(name);
  if (key.empty()) {
    report.error = "the file name cannot be used as a layout name";
    return report;
  }
  report.key = key;
  std::string error;
  std::optional<LoadResult> loaded = parse(text, error);
  if (!loaded) {
    report.error = "not a layout file: " + error;
    return report;
  }
  if (store_.exists(mode_, key) && !overwrite) {
    report.error = "a layout named \"" + key + "\" already exists";
    return report;
  }
  report = reportFrom(*loaded, key);
  if (const Status s = store_.write(mode_, key, text); !s) {
    report.ok = false;
    report.error = s.error;
  }
  return report;
}

LayoutReport LayoutManager::importFile(const std::filesystem::path& file, bool overwrite) {
  LayoutReport report;
  std::string name = file.stem().string();
  if (name.size() > 7 && name.compare(name.size() - 7, 7, ".layout") == 0) name.resize(name.size() - 7);
  if (const std::string key = userKey(name) ? name : keyFromName(name); !key.empty()) {
    if (const std::optional<std::filesystem::path> own = store_.pathOf(mode_, key)) {
      std::error_code ec;
      if (std::filesystem::equivalent(*own, file, ec) && !ec) {
        report.error = "a layout cannot be imported onto itself";
        return report;
      }
    }
  }
  const std::optional<std::string> text = readBoundedFile(file);
  if (!text) {
    report.error = "the file cannot be read (it is missing, unreadable or larger than 4 MiB)";
    return report;
  }
  return importText(name, *text, overwrite);
}

Status LayoutManager::exportFile(const std::filesystem::path& file, std::string_view key, std::string_view name,
                                 std::string_view description) const {
  if (file.empty()) return Status::failure("no file was chosen");
  if (!key.empty()) {
    if (!userKey(key)) return Status::failure("not a usable layout name");
    const std::optional<std::string> text = store_.read(mode_, key);
    if (!text) return Status::failure("no such layout");
    return writeFileAtomic(file, *text);
  }
  DockLayout copy = target_.currentLayout();
  const std::string display = cleaned(trimmed(name), kMaxNameBytes);
  LayoutMeta meta = copy.meta();
  if (!display.empty()) meta.name = display;
  if (!description.empty()) meta.description = cleaned(description, kMaxDescriptionBytes);
  if (const Status s = copy.setMeta(std::move(meta)); !s) return s;
  return writeFileAtomic(file, copy.toJson());
}

}  // namespace r1ui::dock
