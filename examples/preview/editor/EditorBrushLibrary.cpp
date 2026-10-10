// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the Editor's brush library wiring (slice 5.23): the sample brushes, loading and saving the user's
//   favourites, recents and letters (brushes.json under the data root), the BrushLibraryController with its
//   command "brush.library" (key B, rebindable in the hotkey editor), the pictures, and the active brush
//   shown in the status line and the viewport.
// Invariants: the active brush is always a listed brush id or empty; at start it is the most recent pick of
//   the previous session when that brush is still listed, else "Standard"; picking a brush records it as a
//   recent one (the controller does), sets the model's active id, the viewport's brush name and the status
//   line, and writes one console line; a damaged brushes.json is kept aside by the store and the defaults
//   apply with a warning in the console.
// Callers: EditorApp's constructor (startBrushLibrary), the library's pick handler (selectBrush).
#include "EditorApp.h"
#include "EditorMenus.h"

namespace preview::editor {

namespace cb = r1ui::commands::brushes;

void EditorApp::startBrushLibrary() {
  const cb::SetBrushesResult listed = brushModel_.setBrushes(sampleBrushes());
  for (const cb::BrushIssue& issue : listed.issues) model_.log(LogLevel::Warning, "brush " + issue.where + ": " + issue.message);

  const cb::BrushStateLoadReport loaded = brushStorage_.load();
  if (!loaded.ok) model_.log(LogLevel::Warning, "brushes.json was not used: " + loaded.error);
  if (!loaded.keptAside.empty()) model_.log(LogLevel::Warning, "brushes.json was damaged and kept as " + loaded.keptAside);
  for (const cb::BrushStateIssue& issue : loaded.issues) model_.log(LogLevel::Warning, "brushes.json " + issue.where + ": " + issue.message);

  brushController_ = std::make_unique<r1ui::widgets::BrushLibraryController>(ui_, services(), brushModel_, [this](const std::string& id) { selectBrush(id); });
  if (host_.thumbnailSink != nullptr) brushController_->setThumbnails(&brushThumbs_, host_.thumbnailSink);

  // The brush of the previous session, else the first sample.
  std::string first = brushModel_.recents().empty() ? std::string() : brushModel_.recents().front();
  if (!brushModel_.indexOfId(first)) first = "standard";
  if (const auto index = brushModel_.indexOfId(first)) {
    brushModel_.setActiveId(first);
    model_.brushName = brushModel_.brushes()[*index].name;
  }
}

bool EditorApp::selectBrush(const std::string& brushId) {
  const auto index = brushModel_.indexOfId(brushId);
  if (!index) return false;
  const std::string& name = brushModel_.brushes()[*index].name;
  brushModel_.setActiveId(brushId);
  model_.brushName = name;
  model_.touch();
  model_.info("Brush: " + name);
  setStatus("Brush: " + name);
  registry_.touch();
  return true;
}

}  // namespace preview::editor
