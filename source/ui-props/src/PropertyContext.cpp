// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PropertyContext's selection handling, row derivation (the properties common to every selected
//   type) and the read side: value, mixed detection per row and per axis, default comparison, edit
//   conditions and binding state.
// Why: see PropertyContext.h. Reading never mutates anything and never throws except through a host
//   getter, which is the host's own code.
// Callers: PropertyPanel, hosts, tests. The edit side lives in ContextEdit.cpp, copy/paste in
//   ContextText.cpp.
#include "r1ui/props/PropertyContext.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace r1ui::props {

void EditReport::add(EditCode issueCode, std::string_view property, int64_t target) {
  ++issueCount;
  if (isError(issueCode) && !isError(code)) code = issueCode;
  if (issues.size() < kMaxIssues) issues.push_back({issueCode, std::string(property), target});
}

void EditReport::merge(const EditReport& other) {
  changed += other.changed;
  clamped += other.clamped;
  issueCount += other.issueCount;
  for (const EditIssue& issue : other.issues) {
    if (issues.size() < kMaxIssues) issues.push_back(issue);
  }
  if (isError(other.code) && !isError(code)) code = other.code;
  if (!isError(code)) code = changed > 0 ? (clamped > 0 ? EditCode::Clamped : EditCode::Ok) : EditCode::Unchanged;
}

PropertyContext::PropertyContext(ContextOptions options)
    : undo_(options.undo),
      bindings_(options.bindings ? std::move(options.bindings) : std::make_shared<BindingStore>()),
      provider_(options.provider),
      strings_(std::move(options.strings)) {
  if (undo_ == nullptr) {
    ownedUndo_ = std::make_unique<UndoStack>(options.ownedUndo);
    undo_ = ownedUndo_.get();
  }
  // Undo and redo change values behind the context's back: tell the views (rule 7).
  undoListener_ = undo_->addListener([this](const UndoEvent& e) {
    if (e.kind == UndoEventKind::Undone || e.kind == UndoEventKind::Redone || e.kind == UndoEventKind::Forgotten) {
      notifier_.notify({ChangeKind::Values, -1});
      notifier_.notify({ChangeKind::Structure, -1});
    }
  });
}

PropertyContext::~PropertyContext() { undo_->removeListener(undoListener_); }

void PropertyContext::setProvider(const BindingProvider* provider) {
  provider_ = provider;
  notifier_.notify({ChangeKind::Bindings, -1});
}

void PropertyContext::notifyExternalChange() { notifier_.notify({ChangeKind::Values, -1}); }

// ---- selection and rows ---------------------------------------------------------------------------------

size_t PropertyContext::setSelection(std::span<const Target> targets) {
  targets_.clear();
  sets_.clear();
  rows_.clear();
  rowByName_.clear();

  std::unordered_map<const PropertySetBase*, uint32_t> setIndex;
  std::unordered_set<const void*> seen;
  targets_.reserve(std::min(targets.size(), kMaxTargets));
  for (const Target& t : targets) {
    if (targets_.size() >= kMaxTargets) break;
    if (!t.valid() || !seen.insert(t.object).second) continue;  // invalid or the same object twice
    const auto [it, inserted] = setIndex.try_emplace(t.set, static_cast<uint32_t>(sets_.size()));
    if (inserted) sets_.push_back(t.set);
    targets_.push_back({t, it->second});
  }

  // Rows: the first set's visible properties that every other set also declares with the same kind.
  if (!sets_.empty()) {
    const PropertySetBase& first = *sets_.front();
    for (const size_t p : first.displayOrder()) {
      const PropertyDescriptor& d = first.at(p);
      if (d.meta.hidden) continue;
      Row row;
      row.name = d.name;
      row.property.push_back(static_cast<uint32_t>(p));
      bool common = true;
      for (size_t s = 1; s < sets_.size() && common; ++s) {
        const auto other = sets_[s]->find(d.name);
        common = other && sets_[s]->at(*other).kind == d.kind;
        if (common) row.property.push_back(static_cast<uint32_t>(*other));
      }
      if (common) {
        rowByName_.emplace(row.name, rows_.size());
        rows_.push_back(std::move(row));
      }
    }
  }
  notifier_.notify({ChangeKind::Selection, -1});
  return targets_.size();
}

void PropertyContext::forgetObject(const void* object) {
  undo_->forgetObject(object);
  bindings_->forgetObject(object);
  const bool selected = std::any_of(targets_.begin(), targets_.end(), [&](const TargetInfo& t) { return t.target.object == object; });
  if (!selected) return;
  std::vector<Target> kept;
  kept.reserve(targets_.size());
  for (const TargetInfo& t : targets_) {
    if (t.target.object != object) kept.push_back(t.target);
  }
  setSelection(kept);
}

std::optional<size_t> PropertyContext::findRow(std::string_view name) const {
  const auto it = rowByName_.find(std::string(name));
  if (it == rowByName_.end()) return std::nullopt;
  return it->second;
}

// ---- conditions -------------------------------------------------------------------------------------------

namespace {

bool truthy(const Value& v) {
  switch (storageOf(v)) {
    case Storage::Bool: return std::get<bool>(v);
    case Storage::Int: return std::get<int64_t>(v) != 0;
    case Storage::Double: return std::get<double>(v) != 0.0;
    case Storage::String: return !std::get<std::string>(v).empty();
    default: return true;
  }
}

bool compare(const Value& v, CompareOp op, const Value& operand) {
  const auto a = componentOf(v, 0);
  const auto b = componentOf(operand, 0);
  const bool numeric = a && b && (storageOf(v) == Storage::Int || storageOf(v) == Storage::Double) &&
                       (storageOf(operand) == Storage::Int || storageOf(operand) == Storage::Double);
  switch (op) {
    case CompareOp::Truthy: return truthy(v);
    case CompareOp::Equal: return numeric ? *a == *b : valuesEqual(v, operand);
    case CompareOp::NotEqual: return numeric ? *a != *b : !valuesEqual(v, operand);
    case CompareOp::Less: return numeric && *a < *b;
    case CompareOp::LessEqual: return numeric && *a <= *b;
    case CompareOp::Greater: return numeric && *a > *b;
    case CompareOp::GreaterEqual: return numeric && *a >= *b;
  }
  return true;
}

}  // namespace

// A condition naming a property the object does not have holds (fails open), so a typo in a declaration
// never hides a property.
bool PropertyContext::conditionHolds(const Condition& c, const Target& target) const {
  if (!c.present()) return true;
  if (c.predicate != nullptr) return c.predicate(target.object);
  const auto index = target.set->find(c.property);
  if (!index) return true;
  return compare(target.set->at(*index).accessor.get(target.object), c.op, c.operand);
}

// ---- state --------------------------------------------------------------------------------------------------

BindingState PropertyContext::bindingOf(size_t row, std::string& source) const {
  const std::string& name = rows_[row].name;
  size_t bound = 0;
  bool sameSource = true;
  for (const TargetInfo& t : targets_) {
    const BindingRecord* record = bindings_->find(t.target.object, name);
    if (record == nullptr) continue;
    if (bound == 0) source = record->source;
    else if (record->source != source) sameSource = false;
    ++bound;
  }
  if (bound == 0) return BindingState::Unbound;
  if (bound != targets_.size() || !sameSource) {
    source.clear();
    return BindingState::Mixed;
  }
  if (provider_ == nullptr) return BindingState::Broken;
  const auto resolved = provider_->resolve(source);
  if (!resolved) return BindingState::Broken;
  return isError(validate(descriptor(row), *resolved).code) ? BindingState::Broken : BindingState::Bound;
}

PropertyState PropertyContext::state(size_t row) const {
  PropertyState st;
  if (row >= rows_.size() || targets_.empty()) return st;
  const PropertyDescriptor& presentation = descriptor(row);
  st.value = read(row, 0);
  const size_t axes = componentCount(presentation.kind);
  bool allDefault = true;
  bool anyVisible = false;
  for (size_t t = 0; t < targets_.size(); ++t) {
    const PropertyDescriptor& d = targetDescriptor(row, t);
    const Target& target = targets_[t].target;
    const Value v = t == 0 ? st.value : read(row, t);
    if (t > 0 && !valuesEqual(st.value, v)) {
      st.mixed = true;
      for (size_t a = 0; a < axes && a < st.componentMixed.size() && axes > 1; ++a) {
        const auto x = componentOf(st.value, a);
        const auto y = componentOf(v, a);
        if (x && y && !(*x == *y || (std::isnan(*x) && std::isnan(*y)))) st.componentMixed[a] = true;
      }
    }
    if (!d.writable()) st.readOnly = true;
    if (!conditionHolds(d.meta.enableWhen, target)) st.enabled = false;
    if (conditionHolds(d.meta.visibleWhen, target)) anyVisible = true;
    if (!d.meta.defaultValue) {
      allDefault = false;
    } else if (!valuesEqual(v, *d.meta.defaultValue)) {
      st.differsFromDefault = true;
    }
  }
  st.hasDefault = allDefault;
  st.visible = anyVisible;
  st.binding = bindingOf(row, st.bindingSource);
  st.canReset = presentation.meta.resettable && !st.readOnly && ((st.hasDefault && st.differsFromDefault) || st.binding != BindingState::Unbound);
  return st;
}

}  // namespace r1ui::props
