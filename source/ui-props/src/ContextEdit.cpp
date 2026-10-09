// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the edit side of PropertyContext: the shared apply path (validate all objects, apply to all,
//   roll back on any failure, record one undo step), the public edit operations built on it (set, set
//   component, typed text and expressions, reset, interactions) and binding (bind, unbind, refresh).
// Why: every edit must behave identically regarding validation, clamping, rollback and history, so there
//   is exactly one place that mutates host objects: PropertyContext::apply.
// Invariants: validation happens for every object before the first setter runs; a setter that returns
//   false or throws reverts the objects already written in this call, and nothing is recorded; the undo
//   record is made only after every object accepted the value.
// Callers: PropertyPanel, hosts, tests.
#include <cmath>
#include <limits>

#include "r1ui/props/PropertyContext.h"

namespace r1ui::props {

namespace {

EditReport refusal(EditCode code, std::string_view property) {
  EditReport r;
  r.add(code, property, -1);
  return r;
}

// The kind accepts "Mixed + 5" style edits: a number, or one axis of a vector.
bool numericEdit(ValueKind kind, int component) {
  return kind == ValueKind::Int || kind == ValueKind::Double || kind == ValueKind::Range || (componentCount(kind) > 1 && component >= 0);
}

}  // namespace

// ---- the shared apply path ------------------------------------------------------------------------------

template <class ValueFor>
EditReport PropertyContext::apply(size_t row, const ApplyMode& mode, ValueFor&& valueFor) {
  if (row >= rows_.size()) return refusal(EditCode::NoSuchProperty, {});
  if (targets_.empty()) return refusal(EditCode::NoSelection, rows_[row].name);
  const std::string& name = rows_[row].name;

  // Gate: read-only and (unless the caller resets) disabled rows refuse edits as a whole.
  for (size_t t = 0; t < targets_.size(); ++t) {
    const PropertyDescriptor& d = targetDescriptor(row, t);
    if (!d.writable()) return refusal(EditCode::ReadOnly, name);
    if (mode.requireEnabled && !conditionHolds(d.meta.enableWhen, targets_[t].target)) return refusal(EditCode::Disabled, name);
  }

  // Phase 1: propose and validate for every object; nothing is written yet.
  struct Pending {
    size_t target;
    Value before;
    Value after;
  };
  EditReport report;
  std::vector<Pending> pending;
  std::vector<size_t> detach;
  for (size_t t = 0; t < targets_.size(); ++t) {
    const PropertyDescriptor& d = targetDescriptor(row, t);
    Value current = read(row, t);
    std::optional<Value> proposed = valueFor(t, current);
    if (!proposed) return refusal(EditCode::Unparsable, name);
    Validated checked = validate(d, std::move(*proposed));
    if (isError(checked.code)) {
      EditReport failed;
      failed.add(checked.code, name, static_cast<int64_t>(t));
      return failed;
    }
    if (checked.code == EditCode::Clamped) {
      ++report.clamped;
      report.add(EditCode::Clamped, name, static_cast<int64_t>(t));
    }
    if (mode.detachBinding && bindings_->find(targets_[t].target.object, name) != nullptr) detach.push_back(t);
    if (!valuesEqual(checked.value, current)) pending.push_back({t, std::move(current), std::move(checked.value)});
  }
  if (pending.empty() && detach.empty()) {
    report.code = EditCode::Unchanged;
    return report;
  }

  // Phase 2: write. Any refusal or exception reverts what this call already wrote.
  if (!undo_->begin(mode.label, mode.mergeable)) return refusal(EditCode::InteractionOpen, name);
  size_t written = 0;
  EditCode failure = EditCode::Ok;
  int64_t failedTarget = -1;
  for (; written < pending.size(); ++written) {
    const Pending& p = pending[written];
    try {
      if (!targetDescriptor(row, p.target).accessor.set(targets_[p.target].target.object, p.after)) failure = EditCode::Rejected;
    } catch (...) {
      failure = EditCode::SetterThrew;
    }
    if (failure != EditCode::Ok) {
      failedTarget = static_cast<int64_t>(p.target);
      break;
    }
  }
  if (failure != EditCode::Ok) {
    for (size_t i = written; i-- > 0;) {
      try {
        targetDescriptor(row, pending[i].target).accessor.set(targets_[pending[i].target].target.object, pending[i].before);
      } catch (...) {
        report.add(EditCode::SetterThrew, name, static_cast<int64_t>(pending[i].target));  // the revert itself failed
      }
    }
    undo_->commit();  // balances begin(); nothing was recorded
    report.add(failure, name, failedTarget);
    report.changed = 0;
    notifier_.notify({ChangeKind::Values, static_cast<int64_t>(row)});  // a half-failed setter may have changed other state
    return report;
  }

  // Phase 3: record, detach bindings, close the step.
  for (Pending& p : pending) {
    const Target& target = targets_[p.target].target;
    undo_->recordValue({target.object, target.set, rows_[row].property[targets_[p.target].set], std::move(p.before), std::move(p.after)});
  }
  for (const size_t t : detach) {
    const void* object = targets_[t].target.object;
    const BindingRecord* existing = bindings_->find(object, name);
    BindingChange change{bindings_, object, name, existing != nullptr ? std::optional<BindingRecord>(*existing) : std::nullopt, std::nullopt};
    bindings_->set(object, name, std::nullopt);
    undo_->recordBinding(std::move(change));
  }
  undo_->commit();
  report.changed = pending.size();
  report.code = report.clamped > 0 ? EditCode::Clamped : EditCode::Ok;
  notifier_.notify({ChangeKind::Values, static_cast<int64_t>(row)});
  if (!detach.empty()) notifier_.notify({ChangeKind::Bindings, static_cast<int64_t>(row)});
  return report;
}

// ---- public edits -----------------------------------------------------------------------------------------

EditReport PropertyContext::setValue(size_t row, Value value, EditOptions options) {
  if (row >= rows_.size()) return refusal(EditCode::NoSuchProperty, {});
  const ApplyMode mode{label(strings_.set, descriptor(row)), options.mergeable, true, true, true};
  return apply(row, mode, [&](size_t, const Value&) -> std::optional<Value> { return value; });
}

EditReport PropertyContext::setComponent(size_t row, size_t component, double value, EditOptions options) {
  if (row >= rows_.size()) return refusal(EditCode::NoSuchProperty, {});
  const PropertyDescriptor& d = descriptor(row);
  if (component >= componentCount(d.kind)) return refusal(EditCode::WrongType, d.name);
  if (componentCount(d.kind) == 1) return setValue(row, Value(value), options);
  const ApplyMode mode{label(strings_.set, d), options.mergeable, true, true};
  return apply(row, mode, [&](size_t, const Value& current) -> std::optional<Value> { return withComponent(current, component, value); });
}

EditReport PropertyContext::setComputed(size_t row, int component, const std::function<double(double)>& fn, EditOptions options) {
  if (row >= rows_.size()) return refusal(EditCode::NoSuchProperty, {});
  const PropertyDescriptor& d = descriptor(row);
  if (!numericEdit(d.kind, component) || !fn) return refusal(EditCode::WrongType, d.name);
  const size_t axis = componentCount(d.kind) > 1 ? static_cast<size_t>(component) : 0;
  if (axis >= componentCount(d.kind)) return refusal(EditCode::WrongType, d.name);
  const ApplyMode mode{label(strings_.set, d), options.mergeable, true, true, true};
  return apply(row, mode, [&](size_t, const Value& current) -> std::optional<Value> {
    const auto now = componentOf(current, axis);
    if (!now) return std::nullopt;
    const double result = fn(*now);
    return componentCount(d.kind) > 1 ? withComponent(current, axis, result) : Value(result);
  });
}

EditReport PropertyContext::setText(size_t row, std::string_view text, int component, EditOptions options) {
  if (row >= rows_.size()) return refusal(EditCode::NoSuchProperty, {});
  const ApplyMode mode{label(strings_.set, descriptor(row)), options.mergeable, true, true, true};
  return editText(row, text, component, mode);
}

EditReport PropertyContext::editText(size_t row, std::string_view text, int component, const ApplyMode& mode) {
  const PropertyDescriptor& d = descriptor(row);
  if (text.size() > kMaxPasteBytes) return refusal(EditCode::TooLong, d.name);

  if (numericEdit(d.kind, component)) {
    std::vector<UnitConversion> units = d.meta.units;
    if (!d.meta.unit.empty()) units.push_back({d.meta.unit, 1.0});
    const auto expression = parseExpression(text, {.units = units, .allowMixed = mode.allowMixed});
    if (!expression) return refusal(EditCode::Unparsable, d.name);
    const size_t axis = componentCount(d.kind) > 1 ? static_cast<size_t>(component) : 0;
    if (axis >= componentCount(d.kind)) return refusal(EditCode::WrongType, d.name);
    return apply(row, mode, [&](size_t, const Value& current) -> std::optional<Value> {
      const auto now = componentOf(current, axis);
      if (!now) return std::nullopt;
      const double result = expression->evaluate(*now);  // NaN is refused by validate as NotFinite
      return componentCount(d.kind) > 1 ? withComponent(current, axis, result) : Value(result);
    });
  }
  return apply(row, mode, [&](size_t t, const Value&) { return parseText(targetDescriptor(row, t), text); });
}

EditReport PropertyContext::resetRow(size_t row, bool requireEnabled) {
  const PropertyDescriptor& d = descriptor(row);
  if (!d.meta.resettable) return refusal(EditCode::NoDefault, d.name);
  for (size_t t = 0; t < targets_.size(); ++t) {
    if (!targetDescriptor(row, t).hasDefault()) return refusal(EditCode::NoDefault, d.name);
  }
  const ApplyMode mode{label(strings_.reset, d), false, requireEnabled, true};
  return apply(row, mode, [&](size_t t, const Value&) -> std::optional<Value> { return *targetDescriptor(row, t).meta.defaultValue; });
}

EditReport PropertyContext::reset(size_t row) {
  if (row >= rows_.size()) return refusal(EditCode::NoSuchProperty, {});
  return resetRow(row, false);
}

bool PropertyContext::rowInScope(const PropertyDescriptor& d, const std::optional<std::string_view>& category, const std::optional<std::string_view>& group) const {
  if (category && d.category != *category) return false;
  return !group || d.group == *group;
}

EditReport PropertyContext::resetCategory(std::optional<std::string_view> category, std::optional<std::string_view> group) {
  EditReport total;
  if (targets_.empty()) return refusal(EditCode::NoSelection, {});
  const std::string scope = group ? std::string(*group) : (category ? std::string(*category) : std::string());
  if (!undo_->begin(strings_.reset + (scope.empty() ? std::string("properties") : scope), false)) return refusal(EditCode::InteractionOpen, {});
  for (size_t row = 0; row < rows_.size(); ++row) {
    const PropertyDescriptor& d = descriptor(row);
    if (!rowInScope(d, category, group) || !d.meta.resettable) continue;
    bool eligible = true;
    for (size_t t = 0; t < targets_.size() && eligible; ++t) {
      const PropertyDescriptor& td = targetDescriptor(row, t);
      eligible = td.hasDefault() && td.writable();
    }
    if (!eligible) continue;
    total.merge(resetRow(row, false));
  }
  undo_->commit();
  if (!isError(total.code) && total.changed == 0) total.code = EditCode::Unchanged;
  return total;
}

// ---- interactions -----------------------------------------------------------------------------------------

bool PropertyContext::beginInteraction(size_t row) {
  if (row >= rows_.size() || targets_.empty()) return false;
  if (!undo_->begin(label(strings_.set, descriptor(row)), false)) return false;
  ++interactions_;
  return true;
}

bool PropertyContext::endInteraction(bool mergeable) {
  if (interactions_ == 0) return false;
  --interactions_;
  if (interactions_ == 0 && mergeable) undo_->setMergeable(true);
  const bool closed = undo_->commit();
  if (interactions_ == 0) notifier_.notify({ChangeKind::Structure, -1});  // rows controlled by the edited value may now show or hide
  return closed;
}

bool PropertyContext::cancelInteraction() {
  if (interactions_ == 0) return false;
  interactions_ = 0;
  const bool restored = undo_->cancel();
  notifier_.notify({ChangeKind::Values, -1});
  notifier_.notify({ChangeKind::Structure, -1});
  return restored;
}

// ---- binding ------------------------------------------------------------------------------------------------

EditReport PropertyContext::bind(size_t row, std::string source) {
  if (row >= rows_.size()) return refusal(EditCode::NoSuchProperty, {});
  const PropertyDescriptor& d = descriptor(row);
  if (targets_.empty()) return refusal(EditCode::NoSelection, d.name);
  if (source.empty() || source.size() > kMaxBindingSourceBytes || !isValidUtf8(source)) return refusal(EditCode::Unparsable, d.name);
  for (size_t t = 0; t < targets_.size(); ++t) {
    if (!targetDescriptor(row, t).writable()) return refusal(EditCode::ReadOnly, d.name);
  }

  std::string current;
  const BindingState before = bindingOf(row, current);
  const bool alreadyBound = [&] {
    for (const TargetInfo& t : targets_) {
      const BindingRecord* r = bindings_->find(t.target.object, d.name);
      if (r == nullptr || r->source != source) return false;
    }
    return true;
  }();
  if (alreadyBound) {
    EditReport same;
    same.code = EditCode::Unchanged;
    return same;
  }

  if (!undo_->begin(label(before == BindingState::Unbound ? strings_.bind : strings_.rebind, d), false)) return refusal(EditCode::InteractionOpen, d.name);
  EditReport report;
  report.code = EditCode::Ok;
  // A source that resolves to a value of the right kind is written to every object in the same step.
  const std::optional<Value> resolved = provider_ != nullptr ? provider_->resolve(source) : std::nullopt;
  if (resolved && !isError(validate(d, *resolved).code)) {
    const ApplyMode mode{label(strings_.bind, d), false, true, false};
    report = apply(row, mode, [&](size_t, const Value&) -> std::optional<Value> { return *resolved; });
    if (isError(report.code)) {
      undo_->commit();
      return report;
    }
  }
  for (size_t t = 0; t < targets_.size(); ++t) {
    const void* object = targets_[t].target.object;
    const BindingRecord* existing = bindings_->find(object, d.name);
    BindingChange change{bindings_, object, d.name, existing != nullptr ? std::optional<BindingRecord>(*existing) : std::nullopt, BindingRecord{source}};
    if (!bindings_->set(object, d.name, BindingRecord{source})) {
      undo_->cancel();  // the store is full: roll the partial bind (values and records) back
      EditReport full;
      full.add(EditCode::Rejected, d.name, static_cast<int64_t>(t));
      return full;
    }
    undo_->recordBinding(std::move(change));
  }
  undo_->commit();
  report.changed = report.changed > 0 ? report.changed : targets_.size();
  report.code = EditCode::Ok;
  notifier_.notify({ChangeKind::Bindings, static_cast<int64_t>(row)});
  return report;
}

EditReport PropertyContext::unbind(size_t row) {
  if (row >= rows_.size()) return refusal(EditCode::NoSuchProperty, {});
  const PropertyDescriptor& d = descriptor(row);
  if (targets_.empty()) return refusal(EditCode::NoSelection, d.name);
  EditReport report;
  report.code = EditCode::Unchanged;
  if (!undo_->begin(label(strings_.unbind, d), false)) return refusal(EditCode::InteractionOpen, d.name);
  for (const TargetInfo& t : targets_) {
    const BindingRecord* existing = bindings_->find(t.target.object, d.name);
    if (existing == nullptr) continue;
    BindingChange change{bindings_, t.target.object, d.name, *existing, std::nullopt};
    bindings_->set(t.target.object, d.name, std::nullopt);
    undo_->recordBinding(std::move(change));
    ++report.changed;
  }
  undo_->commit();
  if (report.changed > 0) {
    report.code = EditCode::Ok;
    notifier_.notify({ChangeKind::Bindings, static_cast<int64_t>(row)});
  }
  return report;
}

size_t PropertyContext::refreshBindings() {
  if (bindings_->size() == 0 || provider_ == nullptr) return 0;
  size_t changed = 0;
  for (size_t row = 0; row < rows_.size(); ++row) {
    const std::string& name = rows_[row].name;
    for (size_t t = 0; t < targets_.size(); ++t) {
      const BindingRecord* record = bindings_->find(targets_[t].target.object, name);
      if (record == nullptr) continue;
      const auto resolved = provider_->resolve(record->source);
      if (!resolved) continue;
      const PropertyDescriptor& d = targetDescriptor(row, t);
      const Validated checked = validate(d, *resolved);
      if (isError(checked.code) || !d.accessor.writable()) continue;
      try {
        if (valuesEqual(checked.value, read(row, t))) continue;
        if (d.accessor.set(targets_[t].target.object, checked.value)) ++changed;
      } catch (...) {
        // A failing setter keeps its old value; the binding stays and is retried at the next refresh.
      }
    }
  }
  if (changed > 0) notifier_.notify({ChangeKind::Values, -1});
  return changed;
}

}  // namespace r1ui::props
