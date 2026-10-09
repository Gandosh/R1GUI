// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: UndoStack, the history of property edits: transactions (begin, commit, cancel) that collect the
//   value and binding changes of one user gesture into one labelled step; grouping of rapid repeated
//   changes to the same properties into one step within a time window; undo and redo with redo
//   truncation on a new edit; a memory budget that drops the oldest steps; change listeners; and the
//   hook that lets a focused text field undo its own typing first.
// Why: spec 09 rules 74-83. A drag-scrub is one step however many intermediate values were applied, a
//   committed edit is one step, several properties changed by one gesture share a step, an edit that
//   changes nothing records nothing, and the history is bounded by memory, not by step count.
// Callers: PropertyContext (records every change), hosts (undo/redo commands, history menus), the
//   property panel (listens to refresh), tests.
// Transactions: begin() opens a transaction or joins the open one (rule 77: an edit made while another
//   is in progress joins it); commit() closes one level and finalises the step when the outermost level
//   closes; cancel() rolls every recorded change back and closes the whole transaction (the interaction
//   was abandoned). Changes to the same (object, property) inside one transaction collapse to the first
//   old value and the last new value; when that equals the start the change is dropped, so a scrub that
//   returns to its start records no step (rule 31).
// Grouping: a step committed with mergeable = true joins the previous step when that one is also
//   mergeable, covers exactly the same (object, property) keys, and was committed less than
//   groupWindowMs ago on the injected Clock (default 400 ms; spec 09 names no window for repeated
//   wheel or arrow steps, so this is a tunable). If the merged step ends where the earlier one started
//   the step disappears (redundant). Typed commits, resets and pastes are never mergeable.
// Budget: bytes are estimated per step (entries plus string payloads); after every commit the oldest
//   steps are dropped until the total fits (default 256 MiB, rule 80). A single step larger than the
//   whole budget is dropped as well and reported as Evicted.
// Failure behavior: a setter that fails or throws during undo/redo rolls the partial application back,
//   leaves the step where it was and reports the failure; nothing is half applied.
// Lifetime: steps hold raw object and PropertySet pointers. The host calls forgetObject() before it
//   destroys an object (PropertyContext::forgetObject does this) and keeps every PropertySet alive as
//   long as the stack. Binding steps hold weak references to the store.
// Threading: single threaded. Listener callbacks must not call back into the stack's mutating methods
//   (they return false while listeners run).
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "r1ui/props/Binding.h"
#include "r1ui/props/PropertySet.h"

namespace r1ui::props {

// ---- clocks ---------------------------------------------------------------------------------------------

class Clock {
 public:
  virtual ~Clock() = default;
  virtual int64_t nowMs() const = 0;
};

class SteadyClock final : public Clock {
 public:
  int64_t nowMs() const override {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
};

// A clock the owner advances by hand: tests, and hosts that drive time from their frame loop.
class ManualClock final : public Clock {
 public:
  int64_t nowMs() const override { return now_; }
  void set(int64_t ms) { now_ = ms; }
  void advance(int64_t ms) { now_ += ms; }

 private:
  int64_t now_ = 0;
};

// ---- recorded changes -----------------------------------------------------------------------------------

struct ValueChange {
  void* object = nullptr;
  const PropertySetBase* set = nullptr;
  uint32_t property = 0;  // index in set
  Value before;
  Value after;
};

struct BindingChange {
  std::weak_ptr<BindingStore> store;
  const void* object = nullptr;
  std::string property;
  std::optional<BindingRecord> before;
  std::optional<BindingRecord> after;
};

enum class UndoEventKind : uint8_t { Committed, Merged, Undone, Redone, Cleared, Evicted, Cancelled, Forgotten };

struct UndoEvent {
  UndoEventKind kind = UndoEventKind::Committed;
  std::string label;  // the step concerned (empty for Cleared)
};

struct UndoOptions {
  size_t budgetBytes = size_t{256} << 20;
  int64_t groupWindowMs = 400;
  const Clock* clock = nullptr;  // null: a SteadyClock owned by the stack; a given clock must outlive it
};

// What a focused text field offers: undo/redo of the text typed since it gained focus (spec 09 rule 83).
class TextUndoHook {
 public:
  virtual ~TextUndoHook() = default;
  virtual bool canUndoText() const = 0;
  virtual bool undoText() = 0;
  virtual bool canRedoText() const = 0;
  virtual bool redoText() = 0;
};

enum class UndoRoute : uint8_t { None, Text, Document };

struct UndoResult {
  bool done = false;
  std::string message;  // why it failed
};

// ---- the stack -------------------------------------------------------------------------------------------

class UndoStack {
 public:
  using ListenerId = uint64_t;
  using Listener = std::function<void(const UndoEvent&)>;

  explicit UndoStack(UndoOptions options = {});

  // ---- transactions ----
  // Opens a transaction, or joins the open one (the first label and mergeable flag win). False while a
  // listener is running.
  bool begin(std::string label, bool mergeable = false);
  // Closes one level; the outermost close finalises the step (or records nothing). False when no
  // transaction is open.
  bool commit();
  // Rolls back everything recorded in the open transaction and closes it. Returns false when none is
  // open or a rollback setter failed (the step is then discarded anyway and the failure reported).
  bool cancel();
  bool inTransaction() const { return depth_ > 0; }
  // Marks the open transaction (not) mergeable; a gesture only knows at its end whether it was one notch.
  void setMergeable(bool mergeable) {
    if (depth_ > 0) open_.mergeable = mergeable;
  }
  // Called by PropertyContext after it applied a change. False when no transaction is open.
  bool recordValue(ValueChange change);
  bool recordBinding(BindingChange change);

  // ---- history ----
  UndoResult undo();
  UndoResult redo();
  // Text first when a hook is set and has something to undo/redo, else the document history.
  UndoRoute undoRouted();
  UndoRoute redoRouted();
  void setTextHook(TextUndoHook* hook) { textHook_ = hook; }
  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  size_t undoCount() const { return undo_.size(); }
  size_t redoCount() const { return redo_.size(); }
  // Label of the step undo()/redo() would act on; empty when none.
  std::string undoLabel() const;
  std::string redoLabel() const;
  // Labels newest first, for a history menu.
  std::vector<std::string> undoLabels(size_t limit = 64) const;
  void clear();

  // ---- budget and tuning ----
  size_t usedBytes() const { return usedBytes_; }
  size_t budgetBytes() const { return options_.budgetBytes; }
  void setBudgetBytes(size_t bytes);
  int64_t groupWindowMs() const { return options_.groupWindowMs; }
  void setGroupWindowMs(int64_t ms) { options_.groupWindowMs = ms < 0 ? 0 : ms; }

  // ---- lifetime ----
  // Removes every change that refers to `object` from the history (steps that become empty vanish).
  void forgetObject(const void* object);

  // ---- observation ----
  ListenerId addListener(Listener listener);
  void removeListener(ListenerId id);

 private:
  struct Step {
    std::string label;
    int64_t timeMs = 0;
    bool mergeable = false;
    std::vector<ValueChange> values;
    std::vector<BindingChange> bindings;
    size_t bytes = 0;
  };

  static size_t estimate(const Step& step);
  static void prune(Step& step);
  bool applyStep(const Step& step, bool forward, std::string& failure) const;
  bool rollback(const Step& step);
  void finalise();
  void evict();
  void emit(UndoEventKind kind, const std::string& label);
  int64_t now() const { return options_.clock != nullptr ? options_.clock->nowMs() : fallback_.nowMs(); }

  // Identity of one change inside a step: (object, set, property) for values, (object, store, name) for
  // bindings. The index maps a key to its position in `open_` so recording stays O(1) per object.
  struct Key {
    const void* object = nullptr;
    const void* owner = nullptr;
    uint64_t property = 0;
    std::string name;
    friend bool operator==(const Key&, const Key&) = default;
  };
  struct KeyHash {
    size_t operator()(const Key& k) const;
  };
  static bool keyLess(const Key& a, const Key& b);
  static Key keyOf(const ValueChange& change);
  static Key keyOf(const BindingChange& change);
  static std::vector<Key> keysOf(const Step& step);
  void rebuildIndex();

  UndoOptions options_;
  SteadyClock fallback_;
  std::unordered_map<Key, size_t, KeyHash> openIndex_;
  std::deque<Step> undo_;   // oldest first
  std::vector<Step> redo_;  // next redo last
  Step open_;
  int depth_ = 0;
  bool notifying_ = false;
  size_t usedBytes_ = 0;
  TextUndoHook* textHook_ = nullptr;
  std::vector<std::pair<ListenerId, Listener>> listeners_;
  ListenerId lastListener_ = 0;
};

}  // namespace r1ui::props
