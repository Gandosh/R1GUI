// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the undo/redo stack of the single-line text editor, including grouping of consecutive
//   typing / backspacing / forward-deleting into one undo step.
// Why: TextEditor keeps its editing rules separate from history bookkeeping; this class only
//   stores and merges records and never touches the text itself.
// Callers: TextEditor only (private header).
// Bounds: at most kMaxUndoRecords records and kMaxUndoBytes of stored text; the oldest records
//   are dropped first. The redo stack is cleared by every new edit.
// Grouping: a record merges into the previous one only while the group is open (no caret move,
//   selection change, clipboard operation, undo/redo or explicit break since) and the edits are
//   adjacent and of the same kind.
#pragma once

#include <cstddef>
#include <deque>
#include <string>

namespace r1ui::text {

enum class EditKind { Typing, DeleteBackward, DeleteForward, Other };

struct EditRecord {
  std::size_t offset = 0;      // where `removed` started in the text before the edit
  std::string removed;         // text that was replaced
  std::string inserted;        // text that replaced it
  std::size_t caretBefore = 0;
  std::size_t anchorBefore = 0;
  std::size_t caretAfter = 0;
  std::size_t anchorAfter = 0;
  EditKind kind = EditKind::Other;
};

class EditHistory {
 public:
  static constexpr std::size_t kMaxUndoRecords = 1000;
  static constexpr std::size_t kMaxUndoBytes = std::size_t{8} * 1024 * 1024;

  void clear();
  void record(EditRecord rec);
  void breakGroup() { groupOpen_ = false; }

  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }

  // Move the top record to the other stack and return a copy for the caller to apply.
  EditRecord takeUndo();
  EditRecord takeRedo();

 private:
  bool canMerge(const EditRecord& rec) const;
  void trim();

  std::deque<EditRecord> undo_;
  std::deque<EditRecord> redo_;
  std::size_t bytes_ = 0;
  bool groupOpen_ = false;
};

}  // namespace r1ui::text
