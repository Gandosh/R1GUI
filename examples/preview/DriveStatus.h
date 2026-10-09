// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: driveStatus(), the text snapshot a scripted real-desktop drive reads: where the preview's windows
//   and the Editor's tabs, menu titles and bars are in physical screen pixels, which panels sit in which
//   area, and the status text. Enabled only when the environment variable R1GUI_PREVIEW_STATUS names a
//   file; PreviewApp then rewrites it whenever the text changes.
// Why: a drive that injects real mouse input needs coordinates; reading them from the running app is
//   exact where guessing from a screenshot is not (tests/preview/editor_drive.ps1).
// Callers: PreviewApp (status file), nothing else. UI thread only.
#pragma once

#include <string>

namespace preview {

class PreviewApp;

// One line per window ("main", "float"), area ("area"), panel tab ("tabs"), widget ("widget", "menu") and
// the texts ("text"). Empty outside the Editor screen.
std::string driveStatus(PreviewApp& app);

}  // namespace preview
