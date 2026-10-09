// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: LayoutManager, the headless owner of the active layout's life cycle (spec 04): startup from
//   the auto-saved layout (or the host's default), named layouts (list, save, save as, rename,
//   duplicate, delete), applying a layout at runtime, reset to default with a confirmation
//   callback, auto-save debounced after the last change plus an explicit flush for exit, layouts per
//   application mode, and import and export with validation.
// Why: which layout is active, when it is written and what happens to a damaged file are rules of
//   the product, not of any widget; keeping them headless (storage and time behind interfaces)
//   makes every one of them testable with hostile inputs and a fake clock.
// Callers: the application shell (menu commands, startup, exit), DockHost through ILayoutTarget,
//   tests. Calls: DockLayout (parse, serialise), ILayoutStore, IClock.
// Modes: every mode (application mode, "editor", "asset", ...) has its own scope in the store, its
//   own "_active" layout and its own named layouts. setMode() saves the pending change of the old
//   mode first. The manager does not switch the target by itself: after setMode() the shell calls
//   startup() (or load()) to put the new mode's layout on screen.
// Auto-save (spec 04 rules 8-11): notifyChanged() marks the layout dirty and restarts a countdown of
//   autosaveDelayMs (5 s); tick() writes the active layout when the countdown has run out, unless
//   saving is suspended (a tab drag, a layout switch). A failed write keeps the layout dirty and
//   retries after another delay. flush() writes immediately (application exit).
// Safety: a layout that cannot be read or validated is never applied and never overwritten: on
//   startup it is kept aside under "_corrupt-N" and the default is used; a rejected load, import or
//   reset leaves the current layout and every stored file exactly as they were. Names are checked
//   (keyProblem) before they reach the store. Unknown panels are dropped on load; panels new to the
//   layout appear at the position their module suggests (DockLayout::fromJson).
// Threading: UI thread only; no callbacks are invoked while internal state is half-updated.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/dock/DockLayout.h"
#include "r1ui/dock/LayoutStore.h"

namespace r1ui::dock {

// What the manager needs from the thing that shows the layout (DockHost implements it).
class ILayoutTarget {
 public:
  virtual ~ILayoutTarget() = default;
  // The layout on screen right now (includes window rectangles as last reported).
  virtual const DockLayout& currentLayout() const = 0;
  // Replaces the layout on screen. Panels that stay open keep their content state. A failure leaves
  // the old layout in place.
  virtual Status applyLayout(DockLayout layout) = 0;
  // The panels the application registers now, and the model tunables to load with.
  virtual std::vector<PanelInfo> panels() const = 0;
  virtual DockConfig config() const = 0;
};

struct LayoutManagerOptions {
  uint64_t autosaveDelayMs = 5000;  // spec 04 rule 8
  size_t maxWarnings = 50;
};

// What a load, import or startup reports (never throws; `error` is empty exactly when `ok`).
struct LayoutReport {
  bool ok = false;
  std::string error;
  std::string key;                        // the layout that was loaded or imported
  size_t droppedPanels = 0;
  size_t duplicatePanels = 0;
  size_t repairedValues = 0;
  size_t windowsMoved = 0;
  size_t newPanelsPlaced = 0;
  std::vector<std::string> warnings;
  bool usedDefault = false;               // startup: the default layout was applied instead
  std::string keptAsideAs;                // startup: a damaged layout was moved to this key
};

struct LayoutSummary {
  std::string key;
  std::string displayName;   // the stored name, else the key made readable (spec 04 rule 18)
  std::string description;
  bool active = false;       // the layout most recently loaded or saved as
};

class LayoutManager {
 public:
  LayoutManager(ILayoutStore& store, IClock& clock, ILayoutTarget& target, LayoutManagerOptions options = {});

  // ---- mode, defaults, monitors ----
  // Saves any pending change of the old mode, then switches scope. `mode` must pass keyProblem.
  Status setMode(std::string mode);
  const std::string& mode() const { return mode_; }
  // The default layout of the current mode (spec 04 rule 16); it is built on demand.
  void setDefaultProvider(std::function<DockLayout()> provider) { defaultProvider_ = std::move(provider); }
  // Connected displays, for bringing unreachable windows back (decision D13).
  void setMonitorProvider(std::function<MonitorSet()> provider) { monitorProvider_ = std::move(provider); }

  // ---- startup ----
  // Applies the mode's "_active" layout; when it is missing, damaged, too new or unusable applies
  // the default (a damaged file is kept aside, never deleted or overwritten).
  LayoutReport startup();

  // ---- named layouts (user layouts; keys never start with '_') ----
  std::vector<LayoutSummary> list() const;
  // Overwrites an existing layout with the current arrangement, keeping its name and description.
  Status save(std::string_view key);
  // Creates a layout named `name` (any UTF-8 text up to 256 bytes; the key is derived from it).
  // Fails, with the reason, for an unusable or an existing name. `keyOut` receives the key.
  Status saveAs(std::string_view name, std::string_view description, std::string* keyOut = nullptr);
  Status rename(std::string_view key, std::string_view newName, std::string* keyOut = nullptr);
  // A copy under a new name; the original name and description are not carried over unless asked.
  Status duplicate(std::string_view key, std::string_view newName, bool keepDescription = false, std::string* keyOut = nullptr);
  Status remove(std::string_view key);
  // Makes the stored layout the active one. On any failure nothing changes.
  LayoutReport load(std::string_view key);

  // ---- reset ----
  struct ResetOutcome {
    bool done = false;
    bool cancelled = false;
    std::string error;
  };
  // Asks `confirm` (with the question to show); on yes applies the default layout.
  ResetOutcome resetToDefault(const std::function<bool(std::string_view question)>& confirm);

  // ---- auto-save ----
  void notifyChanged();
  void tick();
  std::optional<uint64_t> msUntilSave() const;
  // Writes the active layout now when it is dirty (or always with `force`).
  Status flush(bool force = false);
  void setSuspended(bool suspended);
  bool dirty() const { return dirty_; }
  const std::string& lastError() const { return lastError_; }
  // Key of the layout loaded or saved last; empty for the default or an unnamed arrangement.
  const std::string& activeKey() const { return activeKey_; }

  // ---- import and export ----
  // Validates `text` against the registered panels and stores it under the key derived from `name`
  // (or `name` itself when it is a legal key). A rejected layout changes nothing.
  LayoutReport importText(std::string_view name, std::string_view text, bool overwrite = false);
  LayoutReport importFile(const std::filesystem::path& file, bool overwrite = false);
  // Writes the current arrangement (with the given name and description) or a stored layout (key
  // not empty) to any file.
  Status exportFile(const std::filesystem::path& file, std::string_view key = {}, std::string_view name = {},
                    std::string_view description = {}) const;

 private:
  static constexpr const char* kActiveKey = "_active";

  LayoutReport reportFrom(const LoadResult& loaded, std::string key) const;
  std::optional<LoadResult> parse(std::string_view text, std::string& error) const;
  Status writeActive();
  uint64_t dueFromNow() const;
  std::string readableName(std::string_view key) const;
  std::string keptAsideKey() const;
  std::string newKeyFor(std::string_view display) const;
  LayoutReport applyLoaded(LoadResult loaded, std::string key);
  Status storeCurrentAs(std::string_view key, std::string_view name, std::string_view description);

  ILayoutStore& store_;
  IClock& clock_;
  ILayoutTarget& target_;
  LayoutManagerOptions options_;
  std::string mode_ = "default";
  std::string activeKey_;
  std::function<DockLayout()> defaultProvider_;
  std::function<MonitorSet()> monitorProvider_;
  bool dirty_ = false;
  bool suspended_ = false;
  bool changedWhileSuspended_ = false;
  bool autosaveBlocked_ = false;  // a damaged "_active" could not be kept aside: do not overwrite it
  uint64_t dueMs_ = 0;
  std::string lastError_;
};

}  // namespace r1ui::dock
