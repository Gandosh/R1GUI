// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PropertyContext, the selection model of a property panel. It holds N objects of possibly
//   different types, derives the rows they share (by property name and kind), reads their values with
//   mixed-value detection, and applies every edit to all of them as one undoable step: typed values,
//   expressions over the mixed placeholder, vec components, interactive scrubs, resets (per row and per
//   category), copy and paste (one value, or a group as text), and value binding (bind, unbind, rebind).
// Why: spec 09 rules 4, 37-52, 63-76 and 84 (with D16): the panel widget draws and routes; everything
//   that decides what an edit means, refuses it, groups it or undoes it is here, headless and testable.
// Callers: PropertyPanel (widget layer), hosts (selection, external change notices, undo commands),
//   tests.
// Row model: after setSelection the rows are the properties present with the same name and kind in every
//   selected set, in the first object's display order, minus properties the first set marks hidden. A row
//   index is stable until the next setSelection (which notifies ChangeKind::Selection). Presentation
//   metadata comes from the first set; validation, clamping and enum names use each object's own set.
// Edit semantics: an edit is validated against every object first and applied only if none is refused
//   (type, finiteness, enum membership, text); clamping to the hard range is applied and reported as a
//   warning. A host setter that returns false or throws makes the whole edit roll back, leaving every
//   object and the history unchanged. An edit that changes nothing records no step. Editing a bound
//   property detaches its binding in the same step. Read-only rows and rows whose edit condition is
//   false refuse edits (ReadOnly, Disabled); reset and paste report such rows instead of failing.
// Undo: every change goes through the UndoStack (the context owns one unless the host passes its own),
//   labelled from the property labels ("Set Roughness", "Reset Transform", "Paste Position"). An open
//   interaction (beginInteraction..endInteraction) collects all edits made meanwhile into one step.
// Lifetime: Targets are raw pointers owned by the host. Call forgetObject before destroying a selected
//   object. The context, its undo stack and its binding store are single-threaded.
// Large selections: row construction is O(sets x properties); reading a row or applying an edit is O(N)
//   in the number of objects with one indirect call each (see Descriptor.h on accessor erasure).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "r1ui/props/Binding.h"
#include "r1ui/props/ChangeNotifier.h"
#include "r1ui/props/PropertySet.h"
#include "r1ui/props/UndoStack.h"

namespace r1ui::props {

inline constexpr size_t kMaxTargets = size_t{1} << 21;
inline constexpr size_t kMaxIssues = 8;
inline constexpr size_t kMaxPasteBytes = size_t{16} << 20;
inline constexpr size_t kMaxPasteLines = 200000;

// ---- results ---------------------------------------------------------------------------------------------

struct EditIssue {
  EditCode code = EditCode::Ok;
  std::string property;  // the property name (never a value: passwords must not leak into messages)
  int64_t target = -1;   // index in the selection, or -1 when it concerns the whole edit
};

struct EditReport {
  // Summary: the first error if any; else Clamped if a value was moved into range; else Unchanged when no
  // object changed; else Ok.
  EditCode code = EditCode::Ok;
  size_t changed = 0;       // objects whose value changed
  size_t clamped = 0;       // objects whose value was moved into the hard range
  size_t issueCount = 0;    // all issues, including those not listed
  std::vector<EditIssue> issues;  // the first kMaxIssues
  bool ok() const { return !isError(code); }
  void add(EditCode issueCode, std::string_view property, int64_t target);
  // Adds the counts and issues of a sub-edit and recomputes the summary code.
  void merge(const EditReport& other);
};

// The prefixes of undo step names; the host replaces them to localise. Each should end with a space.
struct ContextStrings {
  std::string set = "Set ";
  std::string reset = "Reset ";
  std::string paste = "Paste ";
  std::string bind = "Bind ";
  std::string unbind = "Unbind ";
  std::string rebind = "Rebind ";
};

struct ContextOptions {
  UndoStack* undo = nullptr;                      // null: the context owns one
  UndoOptions ownedUndo;                          // options of the owned stack
  const BindingProvider* provider = nullptr;      // null: bindings are always Broken
  std::shared_ptr<BindingStore> bindings;         // null: a new store
  ContextStrings strings;
};

struct EditOptions {
  bool mergeable = false;  // step-like edits (wheel, arrows) may group within the undo window
};

struct PropertyState {
  Value value;                             // the first object's value (also when mixed)
  bool mixed = false;
  std::array<bool, 4> componentMixed{};    // per axis of a vec2/vec3, per channel (r, g, b, a) of a colour
  bool hasDefault = false;
  bool differsFromDefault = false;         // on any object
  bool canReset = false;                   // resettable, writable and differs: the reset affordance shows
  bool enabled = true;                     // false: the enable condition fails on some object
  bool visible = true;                     // false: the visibility condition fails on every object
  bool readOnly = false;
  BindingState binding = BindingState::Unbound;
  std::string bindingSource;               // the shared source when Bound or Broken
};

// ---- the context -----------------------------------------------------------------------------------------

class PropertyContext {
 public:
  explicit PropertyContext(ContextOptions options = {});
  ~PropertyContext();
  PropertyContext(const PropertyContext&) = delete;
  PropertyContext& operator=(const PropertyContext&) = delete;

  // ---- selection ----
  // Replaces the selection. Invalid and duplicate targets and everything beyond kMaxTargets are dropped;
  // returns how many were accepted. Open interactions are not affected. Notifies Selection.
  size_t setSelection(std::span<const Target> targets);
  size_t targetCount() const { return targets_.size(); }
  const Target& target(size_t index) const { return targets_[index].target; }
  size_t distinctSetCount() const { return sets_.size(); }
  // Drops `object` from the selection, the history and the binding store (call before freeing it).
  void forgetObject(const void* object);

  // ---- rows ----
  size_t rowCount() const { return rows_.size(); }
  const PropertyDescriptor& descriptor(size_t row) const { return rowDescriptor(row, 0); }
  std::optional<size_t> findRow(std::string_view name) const;
  PropertyState state(size_t row) const;

  // ---- editing (all return a report; nothing throws) ----
  // Sets the row to `value` on every object. A Vec row accepts a whole vector.
  EditReport setValue(size_t row, Value value, EditOptions options = {});
  // Sets one axis of a vec2/vec3 row or one channel of a colour row (the others keep each object's own
  // value).
  EditReport setComponent(size_t row, size_t component, double value, EditOptions options = {});
  // Sets each object's value (or one axis/channel, component >= 0) to fn(its current number): the widget
  // layer's hook for an expression it parsed itself ("Mixed + 5"). NaN from fn is refused as NotFinite.
  EditReport setComputed(size_t row, int component, const std::function<double(double)>& fn, EditOptions options = {});
  // Typed text: numbers with units, arithmetic, and "Mixed" relative edits for numeric rows (component
  // selects an axis of a vec row, -1 otherwise); enum names, bool words, colours, vectors and strings for
  // the others. Unparsable text changes nothing.
  EditReport setText(size_t row, std::string_view text, int component = -1, EditOptions options = {});
  // Back to the default on every object (binding detached). Rows without a default or read-only: refused.
  EditReport reset(size_t row);
  // Resets every differing, writable row of the category (and group when given) in one step. A category of
  // nullopt means every row; an empty string names the properties declared without a category.
  EditReport resetCategory(std::optional<std::string_view> category, std::optional<std::string_view> group = std::nullopt);

  // ---- interactions (drag scrubs): everything between begin and end is one undo step ----
  bool beginInteraction(size_t row);
  // Commits the step (nothing recorded when the value ended where it began). `mergeable`: a one-notch
  // gesture (a wheel or arrow step) that may group with the previous one inside the undo window.
  bool endInteraction(bool mergeable = false);
  bool cancelInteraction();  // restores the values from before beginInteraction
  bool interacting() const { return interactions_ > 0; }

  // ---- copy and paste (text) ----
  // The first object's value as text; nullopt for a password row or an unreadable value.
  std::optional<std::string> copyValue(size_t row) const;
  EditReport pasteValue(size_t row, std::string_view text);
  // "name<TAB>value" lines for the rows of a category (and group), skipping passwords. A category of
  // nullopt means every row.
  std::string copyGroup(std::optional<std::string_view> category, std::optional<std::string_view> group = std::nullopt) const;
  // Applies the lines whose names match rows of the scope, in one step; unknown names and rows that are
  // read-only, disabled or whose text does not parse are reported and skipped.
  EditReport pasteGroup(std::optional<std::string_view> category, std::optional<std::string_view> group, std::string_view text);

  // ---- binding (D16) ----
  EditReport bind(size_t row, std::string source);  // also rebinds; writes the resolved value if any
  EditReport unbind(size_t row);                    // keeps the current value
  // Re-resolves every bound property of the selection and stores the new values without an undo step
  // (they are derived). Returns the number of objects whose value changed; notifies Values.
  size_t refreshBindings();
  BindingStore& bindings() { return *bindings_; }
  const BindingProvider* provider() const { return provider_; }
  void setProvider(const BindingProvider* provider);

  // ---- observation and history ----
  ChangeNotifier& notifier() { return notifier_; }
  UndoStack& undo() { return *undo_; }
  // Tells views that values changed outside the context (a script, a gizmo): they refresh.
  void notifyExternalChange();
  const ContextStrings& strings() const { return strings_; }

 private:
  struct Row {
    std::string name;
    std::vector<uint32_t> property;  // index in each distinct set (parallel to sets_)
  };
  struct TargetInfo {
    Target target;
    uint32_t set = 0;  // index in sets_
  };

  const PropertyDescriptor& rowDescriptor(size_t row, size_t set) const { return sets_[set]->at(rows_[row].property[set]); }
  const PropertyDescriptor& targetDescriptor(size_t row, size_t target) const { return rowDescriptor(row, targets_[target].set); }
  Value read(size_t row, size_t target) const { return targetDescriptor(row, target).accessor.get(targets_[target].target.object); }
  bool conditionHolds(const Condition& condition, const Target& target) const;
  BindingState bindingOf(size_t row, std::string& source) const;
  bool rowInScope(const PropertyDescriptor& d, const std::optional<std::string_view>& category, const std::optional<std::string_view>& group) const;
  std::string label(const std::string& prefix, const PropertyDescriptor& d) const { return prefix + d.shownLabel(); }

  // The shared edit path (ContextEdit.cpp). `valueFor(target, current)` proposes each object's new value
  // (nullopt: the text did not parse for that object).
  struct ApplyMode {
    std::string label;
    bool mergeable = false;
    bool requireEnabled = true;
    bool detachBinding = true;
    bool allowMixed = true;  // numeric text may use the Mixed token
  };
  template <class ValueFor>
  EditReport apply(size_t row, const ApplyMode& mode, ValueFor&& valueFor);
  // Parses `text` for the row and applies it with `mode` (typed edits and pastes).
  EditReport editText(size_t row, std::string_view text, int component, const ApplyMode& mode);
  EditReport resetRow(size_t row, bool requireEnabled);
  EditReport bindRow(size_t row, std::optional<std::string> source);

  std::vector<TargetInfo> targets_;
  std::vector<const PropertySetBase*> sets_;
  std::vector<Row> rows_;
  std::unordered_map<std::string, size_t> rowByName_;
  UndoStack* undo_ = nullptr;
  std::unique_ptr<UndoStack> ownedUndo_;
  std::shared_ptr<BindingStore> bindings_;
  const BindingProvider* provider_ = nullptr;
  ContextStrings strings_;
  ChangeNotifier notifier_;
  UndoStack::ListenerId undoListener_ = 0;  // turns undo/redo into change notices (spec 09 rule 7)
  int interactions_ = 0;
};

}  // namespace r1ui::props
