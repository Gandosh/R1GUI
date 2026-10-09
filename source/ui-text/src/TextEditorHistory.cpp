// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: EditHistory implementation (see EditHistory.h for bounds and grouping rules).
// Invariants: bytes_ equals the stored text size of undo_ plus redo_; takeUndo/takeRedo require
//   the matching stack to be non-empty (callers check canUndo/canRedo).
#include "EditHistory.h"

#include <utility>

namespace r1ui::text {

namespace {

std::size_t weight(const EditRecord& r) { return r.removed.size() + r.inserted.size(); }

}  // namespace

void EditHistory::clear() {
  undo_.clear();
  redo_.clear();
  bytes_ = 0;
  groupOpen_ = false;
}

bool EditHistory::canMerge(const EditRecord& rec) const {
  if (!groupOpen_ || undo_.empty()) return false;
  const EditRecord& last = undo_.back();
  if (last.kind != rec.kind || rec.caretBefore != last.caretAfter) return false;
  switch (rec.kind) {
    case EditKind::Typing:
      return rec.removed.empty() && rec.offset == last.offset + last.inserted.size();
    case EditKind::DeleteBackward:
      return rec.inserted.empty() && last.inserted.empty() && rec.offset + rec.removed.size() == last.offset;
    case EditKind::DeleteForward:
      return rec.inserted.empty() && last.inserted.empty() && rec.offset == last.offset;
    case EditKind::Other:
      return false;
  }
  return false;
}

void EditHistory::record(EditRecord rec) {
  for (const EditRecord& r : redo_) bytes_ -= weight(r);
  redo_.clear();

  if (canMerge(rec)) {
    EditRecord& last = undo_.back();
    bytes_ += weight(rec);
    switch (rec.kind) {
      case EditKind::Typing:
        last.inserted += rec.inserted;
        break;
      case EditKind::DeleteBackward:
        last.offset = rec.offset;
        last.removed = rec.removed + last.removed;
        break;
      case EditKind::DeleteForward:
        last.removed += rec.removed;
        break;
      case EditKind::Other:
        break;
    }
    last.caretAfter = rec.caretAfter;
    last.anchorAfter = rec.anchorAfter;
  } else {
    groupOpen_ = rec.kind != EditKind::Other;
    bytes_ += weight(rec);
    undo_.push_back(std::move(rec));
  }
  trim();
}

void EditHistory::trim() {
  while (undo_.size() > 1 && (undo_.size() > kMaxUndoRecords || bytes_ > kMaxUndoBytes)) {
    bytes_ -= weight(undo_.front());
    undo_.pop_front();
  }
}

EditRecord EditHistory::takeUndo() {
  EditRecord rec = std::move(undo_.back());
  undo_.pop_back();
  redo_.push_back(rec);
  groupOpen_ = false;
  return rec;
}

EditRecord EditHistory::takeRedo() {
  EditRecord rec = std::move(redo_.back());
  redo_.pop_back();
  undo_.push_back(rec);
  groupOpen_ = false;
  return rec;
}

}  // namespace r1ui::text
