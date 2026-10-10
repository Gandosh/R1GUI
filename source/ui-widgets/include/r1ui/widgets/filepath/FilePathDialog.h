// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the in-toolkit file path dialog (slice 5.17/5.18): a modal dialog with a text field for a path, a
//   default folder, the list of the files already there (filtered by extension) and the rules that turn
//   what the user typed into a safe path; plus FileListView, the list widget it uses.
// Why: owner requirement 2026-10-10: every file choice of the custom menu and workspace features goes
//   through a simple dialog of the toolkit (a path field with a default folder and a list of existing
//   files); no operating-system file dialog, which would block scripted runs and cannot be themed.
// Callers: the preview (save/load a menu, save/load a workspace, import/export hotkeys), the creator
//   window, the gallery, tests. Calls: Dialog, TextInput, Button, Label, FileListView.
// Behaviour: Open mode needs an existing regular file; Save mode accepts a new name and, when the file
//   exists, asks for a second press (the button turns into "Overwrite"); the extension is appended when
//   the typed name has none; a relative name lives in the default folder; Enter in the field and a double
//   click on a row do what the button does; clicking a row copies its name into the field; Escape cancels.
//   The callback runs once, after the dialog is gone, with the final path; Cancel never calls it.
// Path rules (checkFilePath, headless): at most kMaxPathChars characters, valid UTF-8, no control
//   characters and none of < > " | ? * (a colon only as the drive letter), no Windows device name as file
//   name (CON, NUL, COM1, ...), not a directory, not empty. A Save into a folder that does not exist
//   creates it (failure is reported in the dialog, nothing is chosen).
// Failure behavior: nothing throws; listing and checking swallow file system errors into messages or
//   empty lists.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "r1ui/core/tree/WidgetId.h"
#include "r1ui/widgets/dialog/Dialog.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

inline constexpr size_t kMaxPathChars = 520;
inline constexpr size_t kMaxListedFiles = 500;

enum class FilePathMode : uint8_t { Open, Save };

struct FilePathOptions {
  FilePathMode mode = FilePathMode::Open;
  std::string title;
  std::string description;
  std::filesystem::path folder;  // default folder for relative names and the listing
  std::string extension;         // ".r1mn" (with the dot) or empty for any file
  std::string initialName;       // Save: the suggested file name (no folder)
  std::string actionLabel;       // default "Open" / "Save"
  core::tree::WidgetId owner;
  double width = 540.0;
};

struct FileEntry {
  std::string name;  // file name with extension
  uint64_t bytes = 0;
};

// Regular files of `folder` whose extension matches (ASCII case-insensitive; empty = all), sorted by name
// without regard to case, at most kMaxListedFiles. Empty for a missing or unreadable folder.
std::vector<FileEntry> listFiles(const std::filesystem::path& folder, std::string_view extension);

struct FilePathCheck {
  bool ok = false;
  bool exists = false;       // the target file exists
  std::filesystem::path path;  // final path (valid when ok)
  std::string message;       // why not ok, or a hint
};
FilePathCheck checkFilePath(const FilePathOptions& options, std::string_view typed);

// "12 B", "3.4 KB", "1.2 MB".
std::string formatFileSize(uint64_t bytes);

// Opens the dialog on the overlay layer of `ui`. An invalid handle when the overlay layer is unavailable.
DialogHandle openFilePathDialog(UiContext& ui, FilePathOptions options, std::function<void(const std::filesystem::path&)> onChosen);

// ---- the list of files -------------------------------------------------------------------------

class FileListView final : public WidgetObject {
 public:
  static constexpr double kRowHeight = 26.0;
  const char* typeName() const override { return "FileListView"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override { return Cursor::Default; }
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onPointerWheel(Event& e) override;
  void onDoubleClick(Event& e) override;
  void onKeyDown(Event& e) override;

  void setEntries(std::vector<FileEntry> entries);
  const std::vector<FileEntry>& entries() const { return entries_; }
  int selected() const { return selected_; }
  bool select(int index, bool notify = true);
  void setOnSelect(std::function<void(const FileEntry&)> callback) { onSelect_ = std::move(callback); }
  void setOnActivate(std::function<void(const FileEntry&)> callback) { onActivate_ = std::move(callback); }
  // Window rectangle of a row (empty when out of range or scrolled out).
  core::layout::RectD rowRect(int index) const;
  int rowAt(double x, double y) const;
  double scrollOffset() const { return scroll_; }

 private:
  double viewportHeight() const;
  void setScroll(double offset);

  std::vector<FileEntry> entries_;
  int selected_ = -1;
  int hover_ = -1;
  double scroll_ = 0.0;
  std::function<void(const FileEntry&)> onSelect_;
  std::function<void(const FileEntry&)> onActivate_;
};

}  // namespace r1ui::widgets
