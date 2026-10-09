// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the strip geometry constants of the toolbar edit display and the drop operation helper the strip
//   files share.
// Why: the display (cells, painting) and the input and drop logic place things on the same strip grid.
// Callers: ToolbarEditor.cpp, ToolbarEditorInput.cpp.
#pragma once

#include "r1ui/commands/customize/Customization.h"
#include "r1ui/widgets/customize/DragHub.h"

namespace r1ui::widgets::cust {

inline constexpr double kStripPad = 5.0;        // surface padding (4 plus the border)
inline constexpr double kStripBand = 16.0;      // the eye badges' band along the strip
inline constexpr double kStripSeparator = 4.0;  // a separator cell's length
inline constexpr double kStripSpacer = 28.0;
inline constexpr double kStripTrigger = 12.0;   // the flyout chevron's width

// Runs the operation a drop stands for on `m` (a real call or a preview).
inline commands::customize::EditResult runStripDrop(commands::customize::Customization& m, const DragPayload& payload, const commands::customize::Placement& at) {
  if (payload.kind == DragPayload::Kind::Command) return m.addCommand(at.parent, payload.commandId, at.anchor, at.side);
  return m.move(payload.nodeId, at);
}

}  // namespace r1ui::widgets::cust
