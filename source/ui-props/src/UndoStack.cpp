// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the implementation of UndoStack.h: transaction bookkeeping, step finalisation (pruning no-op
//   changes, redo truncation, grouping, budget eviction), undo/redo application with all-or-nothing
//   rollback, object forgetting and listener delivery.
// Why: see UndoStack.h. The invariant kept here is that the live document and the history never
//   disagree: every setter failure is rolled back before the call returns.
// Callers: PropertyContext*.cpp, hosts, tests.
#include "r1ui/props/UndoStack.h"

#include <algorithm>
#include <functional>
#include <tuple>

namespace r1ui::props {

// ---- keys ---------------------------------------------------------------------------------------------

bool UndoStack::keyLess(const Key& a, const Key& b) {
  return std::tie(a.object, a.owner, a.property, a.name) < std::tie(b.object, b.owner, b.property, b.name);
}

size_t UndoStack::KeyHash::operator()(const Key& k) const {
  size_t h = std::hash<const void*>{}(k.object);
  h = h * 1099511628211ull ^ std::hash<const void*>{}(k.owner);
  h = h * 1099511628211ull ^ std::hash<uint64_t>{}(k.property);
  return h * 1099511628211ull ^ std::hash<std::string>{}(k.name);
}

UndoStack::Key UndoStack::keyOf(const ValueChange& c) { return {c.object, c.set, c.property, {}}; }
UndoStack::Key UndoStack::keyOf(const BindingChange& c) { return {c.object, c.store.lock().get(), 0, c.property}; }

std::vector<UndoStack::Key> UndoStack::keysOf(const Step& step) {
  std::vector<Key> keys;
  keys.reserve(step.values.size() + step.bindings.size());
  for (const ValueChange& c : step.values) keys.push_back(keyOf(c));
  for (const BindingChange& c : step.bindings) keys.push_back(keyOf(c));
  std::sort(keys.begin(), keys.end(), keyLess);
  return keys;
}

void UndoStack::rebuildIndex() {
  openIndex_.clear();
  for (size_t i = 0; i < open_.values.size(); ++i) openIndex_[keyOf(open_.values[i])] = i;
  for (size_t i = 0; i < open_.bindings.size(); ++i) openIndex_[keyOf(open_.bindings[i])] = i;
}

// ---- accounting ---------------------------------------------------------------------------------------

size_t UndoStack::estimate(const Step& step) {
  size_t bytes = sizeof(Step) + step.label.capacity();
  for (const ValueChange& c : step.values) bytes += sizeof(ValueChange) + valueBytes(c.before) + valueBytes(c.after) - 2 * sizeof(Value);
  for (const BindingChange& c : step.bindings) {
    bytes += sizeof(BindingChange) + c.property.capacity();
    if (c.before) bytes += c.before->source.capacity();
    if (c.after) bytes += c.after->source.capacity();
  }
  return bytes;
}

// Drops the changes that end where they started.
void UndoStack::prune(Step& step) {
  std::erase_if(step.values, [](const ValueChange& c) { return valuesEqual(c.before, c.after); });
  std::erase_if(step.bindings, [](const BindingChange& c) { return c.before == c.after; });
}

UndoStack::UndoStack(UndoOptions options) : options_(options) {
  if (options_.groupWindowMs < 0) options_.groupWindowMs = 0;
}

void UndoStack::emit(UndoEventKind kind, const std::string& label) {
  if (listeners_.empty()) return;
  const bool wasNotifying = notifying_;
  notifying_ = true;
  const auto snapshot = listeners_;
  const UndoEvent event{kind, label};
  for (const auto& [id, listener] : snapshot) {
    if (listener) listener(event);
  }
  notifying_ = wasNotifying;
}

UndoStack::ListenerId UndoStack::addListener(Listener listener) {
  const ListenerId id = ++lastListener_;
  listeners_.emplace_back(id, std::move(listener));
  return id;
}

void UndoStack::removeListener(ListenerId id) {
  std::erase_if(listeners_, [&](const auto& entry) { return entry.first == id; });
}

// ---- transactions -------------------------------------------------------------------------------------

bool UndoStack::begin(std::string label, bool mergeable) {
  if (notifying_) return false;
  if (depth_ == 0) {
    open_ = Step{};
    open_.label = std::move(label);
    open_.mergeable = mergeable;
    openIndex_.clear();
  }
  ++depth_;
  return true;
}

bool UndoStack::recordValue(ValueChange change) {
  if (depth_ == 0) return false;
  const Key key = keyOf(change);
  const auto it = openIndex_.find(key);
  if (it != openIndex_.end()) {
    open_.values[it->second].after = std::move(change.after);  // keep the first old value, take the last new one
    return true;
  }
  openIndex_.emplace(key, open_.values.size());
  open_.values.push_back(std::move(change));
  return true;
}

bool UndoStack::recordBinding(BindingChange change) {
  if (depth_ == 0) return false;
  const Key key = keyOf(change);
  const auto it = openIndex_.find(key);
  if (it != openIndex_.end()) {
    open_.bindings[it->second].after = std::move(change.after);
    return true;
  }
  openIndex_.emplace(key, open_.bindings.size());
  open_.bindings.push_back(std::move(change));
  return true;
}

bool UndoStack::commit() {
  if (depth_ == 0) return false;
  if (--depth_ == 0) finalise();
  return true;
}

bool UndoStack::cancel() {
  if (depth_ == 0) return false;
  Step abandoned = std::move(open_);
  open_ = Step{};
  openIndex_.clear();
  depth_ = 0;
  const bool restored = rollback(abandoned);
  emit(UndoEventKind::Cancelled, abandoned.label);
  return restored;
}

// Closes the outermost transaction: prunes, truncates redo, groups or pushes, evicts.
void UndoStack::finalise() {
  Step step = std::move(open_);
  open_ = Step{};
  openIndex_.clear();
  prune(step);
  if (step.values.empty() && step.bindings.empty()) return;  // nothing changed: no step, redo stays valid
  step.timeMs = now();

  for (const Step& dropped : redo_) usedBytes_ -= std::min(usedBytes_, dropped.bytes);
  redo_.clear();

  if (step.mergeable && options_.groupWindowMs > 0 && !undo_.empty()) {
    Step& last = undo_.back();
    if (last.mergeable && step.timeMs - last.timeMs <= options_.groupWindowMs && keysOf(last) == keysOf(step)) {
      usedBytes_ -= std::min(usedBytes_, last.bytes);
      std::unordered_map<Key, size_t, KeyHash> index;
      for (size_t i = 0; i < last.values.size(); ++i) index[keyOf(last.values[i])] = i;
      for (size_t i = 0; i < last.bindings.size(); ++i) index[keyOf(last.bindings[i])] = i;
      for (ValueChange& c : step.values) last.values[index[keyOf(c)]].after = std::move(c.after);
      for (BindingChange& c : step.bindings) last.bindings[index[keyOf(c)]].after = std::move(c.after);
      last.timeMs = step.timeMs;
      prune(last);
      const std::string label = last.label;
      if (last.values.empty() && last.bindings.empty()) {
        undo_.pop_back();  // the group ended where it began: redundant
      } else {
        last.bytes = estimate(last);
        usedBytes_ += last.bytes;
      }
      emit(UndoEventKind::Merged, label);
      return;
    }
  }

  step.bytes = estimate(step);
  usedBytes_ += step.bytes;
  const std::string label = step.label;
  undo_.push_back(std::move(step));
  evict();
  emit(UndoEventKind::Committed, label);
}

void UndoStack::evict() {
  while (usedBytes_ > options_.budgetBytes && !undo_.empty()) {
    const std::string label = undo_.front().label;
    usedBytes_ -= std::min(usedBytes_, undo_.front().bytes);
    undo_.pop_front();
    emit(UndoEventKind::Evicted, label);
  }
  if (usedBytes_ > options_.budgetBytes && !redo_.empty()) {
    for (const Step& dropped : redo_) usedBytes_ -= std::min(usedBytes_, dropped.bytes);
    redo_.clear();
  }
}

void UndoStack::setBudgetBytes(size_t bytes) {
  options_.budgetBytes = bytes;
  evict();
}

// ---- applying steps -----------------------------------------------------------------------------------

// Applies every change of `step` forward (after) or backward (before). On the first failure the changes
// already applied are reverted and false is returned with `failure` set.
bool UndoStack::applyStep(const Step& step, bool forward, std::string& failure) const {
  const auto write = [](const ValueChange& c, const Value& value) {
    return c.set->at(c.property).accessor.set(c.object, value);
  };
  const size_t count = step.values.size();
  const auto at = [&](size_t i) -> const ValueChange& { return forward ? step.values[i] : step.values[count - 1 - i]; };
  size_t applied = 0;
  bool ok = true;
  for (; applied < count && ok; ++applied) {
    const ValueChange& c = at(applied);
    try {
      ok = write(c, forward ? c.after : c.before);
      if (!ok) failure = "the object refused the value of '" + c.set->at(c.property).name + "'";
    } catch (...) {
      ok = false;
      failure = "the object failed while applying '" + c.set->at(c.property).name + "'";
    }
    if (!ok) break;
  }
  if (!ok) {
    for (size_t i = applied; i-- > 0;) {
      const ValueChange& c = at(i);
      try {
        write(c, forward ? c.before : c.after);
      } catch (...) {
        failure += " (rollback also failed)";
      }
    }
    return false;
  }
  for (const BindingChange& c : step.bindings) {
    if (const auto store = c.store.lock()) store->set(c.object, c.property, forward ? c.after : c.before);
  }
  return true;
}

bool UndoStack::rollback(const Step& step) {
  std::string failure;
  return applyStep(step, false, failure);
}

UndoResult UndoStack::undo() {
  if (notifying_) return {false, "cannot undo from a history listener"};
  if (depth_ > 0) return {false, "an edit is in progress"};
  if (undo_.empty()) return {false, "nothing to undo"};
  std::string failure;
  if (!applyStep(undo_.back(), false, failure)) return {false, failure};
  const std::string label = undo_.back().label;
  redo_.push_back(std::move(undo_.back()));
  undo_.pop_back();
  emit(UndoEventKind::Undone, label);
  return {true, {}};
}

UndoResult UndoStack::redo() {
  if (notifying_) return {false, "cannot redo from a history listener"};
  if (depth_ > 0) return {false, "an edit is in progress"};
  if (redo_.empty()) return {false, "nothing to redo"};
  std::string failure;
  if (!applyStep(redo_.back(), true, failure)) return {false, failure};
  const std::string label = redo_.back().label;
  undo_.push_back(std::move(redo_.back()));
  redo_.pop_back();
  emit(UndoEventKind::Redone, label);
  return {true, {}};
}

UndoRoute UndoStack::undoRouted() {
  if (textHook_ != nullptr && textHook_->canUndoText()) return textHook_->undoText() ? UndoRoute::Text : UndoRoute::None;
  return undo().done ? UndoRoute::Document : UndoRoute::None;
}

UndoRoute UndoStack::redoRouted() {
  if (textHook_ != nullptr && textHook_->canRedoText()) return textHook_->redoText() ? UndoRoute::Text : UndoRoute::None;
  return redo().done ? UndoRoute::Document : UndoRoute::None;
}

std::string UndoStack::undoLabel() const { return undo_.empty() ? std::string() : undo_.back().label; }
std::string UndoStack::redoLabel() const { return redo_.empty() ? std::string() : redo_.back().label; }

std::vector<std::string> UndoStack::undoLabels(size_t limit) const {
  std::vector<std::string> labels;
  for (auto it = undo_.rbegin(); it != undo_.rend() && labels.size() < limit; ++it) labels.push_back(it->label);
  return labels;
}

void UndoStack::clear() {
  if (notifying_ || depth_ > 0) return;
  undo_.clear();
  redo_.clear();
  usedBytes_ = 0;
  emit(UndoEventKind::Cleared, {});
}

// ---- forgetting objects -------------------------------------------------------------------------------

void UndoStack::forgetObject(const void* object) {
  bool touched = false;
  const auto scrub = [&](Step& step) {
    const size_t before = step.values.size() + step.bindings.size();
    std::erase_if(step.values, [&](const ValueChange& c) { return c.object == object; });
    std::erase_if(step.bindings, [&](const BindingChange& c) { return c.object == object; });
    const bool changed = step.values.size() + step.bindings.size() != before;
    if (changed) {
      usedBytes_ -= std::min(usedBytes_, step.bytes);
      step.bytes = estimate(step);
      usedBytes_ += step.bytes;
      touched = true;
    }
  };
  for (Step& step : undo_) scrub(step);
  for (Step& step : redo_) scrub(step);
  const auto empty = [&](const Step& s) {
    if (!s.values.empty() || !s.bindings.empty()) return false;
    usedBytes_ -= std::min(usedBytes_, s.bytes);
    return true;
  };
  std::erase_if(undo_, empty);
  std::erase_if(redo_, empty);
  if (depth_ > 0) {
    std::erase_if(open_.values, [&](const ValueChange& c) { return c.object == object; });
    std::erase_if(open_.bindings, [&](const BindingChange& c) { return c.object == object; });
    rebuildIndex();
  }
  if (touched) emit(UndoEventKind::Forgotten, {});
}

}  // namespace r1ui::props
