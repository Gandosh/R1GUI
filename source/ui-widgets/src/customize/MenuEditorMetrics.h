// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the row geometry constants the MenuEditor files share (grip, label and eye zones of a row).
// Why: the display (geometry, painting), its input handling and its drop logic all place things on the
//   same row grid; one place keeps them in step.
// Callers: MenuEditor.cpp, MenuEditorInput.cpp, MenuEditorDrop.cpp.
#pragma once

namespace r1ui::widgets::cust {

inline constexpr double kGripLeft = 4.0;
inline constexpr double kGripWidth = 22.0;
inline constexpr double kEyeWidth = 30.0;
inline constexpr double kLabelLeft = 30.0;

}  // namespace r1ui::widgets::cust
