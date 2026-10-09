// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the auto-save half of LayoutManager: change notification, the debounce countdown, tick(),
//   flush(), suspension and the write of the active layout.
// Why: spec 04 rules 8-11: a save is scheduled 5 s after the last change (each change restarts the
//   countdown, so a continuous drag produces one save), is held off while a tab drag or a layout
//   switch is in progress, and is forced when the application exits.
// Invariants: time only comes from the injected IClock; a failed write keeps the layout dirty and
//   schedules a retry one delay later; a protected (damaged, not kept aside) active layout is never
//   overwritten.
// Callers: the host's change observer (notifyChanged), the shell's timer (tick, msUntilSave), the
//   exit path (flush), the drag controller (setSuspended).
#include "r1ui/dock/LayoutManager.h"

namespace r1ui::dock {

uint64_t LayoutManager::dueFromNow() const {
  const uint64_t now = clock_.nowMs();
  return options_.autosaveDelayMs > UINT64_MAX - now ? UINT64_MAX : now + options_.autosaveDelayMs;
}

void LayoutManager::notifyChanged() {
  if (suspended_) {
    changedWhileSuspended_ = true;
    return;
  }
  dirty_ = true;
  dueMs_ = dueFromNow();
}

void LayoutManager::setSuspended(bool suspended) {
  if (suspended_ == suspended) return;
  suspended_ = suspended;
  if (!suspended && changedWhileSuspended_) {
    changedWhileSuspended_ = false;
    dirty_ = true;
    dueMs_ = dueFromNow();  // the countdown starts when the drag or switch ends
  }
}

void LayoutManager::tick() {
  if (!dirty_ || suspended_ || clock_.nowMs() < dueMs_) return;
  if (!writeActive()) {
    dirty_ = true;
    dueMs_ = dueFromNow();
  }
}

std::optional<uint64_t> LayoutManager::msUntilSave() const {
  if (!dirty_ || suspended_) return std::nullopt;
  const uint64_t now = clock_.nowMs();
  return dueMs_ > now ? dueMs_ - now : 0;
}

Status LayoutManager::flush(bool force) {
  if (!dirty_ && !force) return Status::success();
  const Status written = writeActive();
  if (!written) {
    dirty_ = true;
    dueMs_ = dueFromNow();
  }
  return written;
}

Status LayoutManager::writeActive() {
  if (autosaveBlocked_) {
    dirty_ = false;  // nothing will ever be written, so do not keep retrying
    return Status::failure(lastError_.empty() ? "auto-save is off" : lastError_);
  }
  const Status written = store_.write(mode_, kActiveKey, target_.currentLayout().toJson());
  if (written) {
    dirty_ = false;
    lastError_.clear();
  } else {
    lastError_ = written.error;
  }
  return written;
}

}  // namespace r1ui::dock
