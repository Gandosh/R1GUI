// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the per-widget payload of the preview's widget tree (what a widget looks like and how it
//   reacts), the preview mode enumeration and the preview-specific style rows.
// Why: the toolkit's tree::Widget carries layout, flags and one user slot; the preview keeps the
//   look (style key, text, icon, state bits) in a side table indexed by that slot so painting and
//   measuring stay data driven and the style table stays the only place colours come from.
// Callers: Scene*.cpp, NameField.cpp, tests/preview. Calls: ui-theme (StyleSheet rows).
// Invariants: Node::userIndex is the widget's userData; a node is only reachable through a live
//   widget id; `state` holds only theme::State bits owned by the widget (hover, active, selected,
//   focus), derived looks (disabled, mixed) are not used by the preview.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "r1ui/platform/Window.h"
#include "r1ui/theme/StyleSheet.h"

namespace preview {

// Tab cycles through these in order; the title bar shows one square per entry.
enum class Mode : uint8_t { Widgets, Swatches, Screens, Sandbox };
inline constexpr int kModeCount = 4;
const char* modeName(Mode mode);

enum class NodeKind : uint8_t {
  Box,         // background/border from `style`, optional separator lines
  Text,        // one line of text styled by `textStyle`, measured by the text module
  Icon,        // glyph icon tinted by the Foreground colour of `textStyle`
  IconButton,  // box from `style` (state aware) with `icon` centred, tinted by the box's Foreground
  Swatch,      // colour chip (`swatchRgb`) over the panel, bordered
  TextField,   // the editable single-line field (NameField)
  ChromeGlyph, // title bar button: box from `style`, drawn minimise/maximise/restore/close glyph
  ModeSquare   // title bar mode indicator; `glyph` is the Mode index
};

enum class ChromeGlyphKind : int { Minimize, Maximize, Restore, Close };

// A resolved style kept until the theme revision or the state bits change.
struct CachedStyle {
  std::optional<r1ui::theme::ResolvedStyle> value;
  uint32_t revision = 0;
  uint8_t state = 0;
};

struct Node {
  NodeKind kind = NodeKind::Box;
  std::string style;      // StyleSheet key of the box look; empty = no box
  std::string textStyle;  // StyleSheet key for text/icon colour and size
  std::string text;
  std::string icon;       // icon file name without extension
  int iconPx = 14;        // logical pixels
  uint8_t state = 0;      // theme::State bits set by pointer/focus events
  bool interactive = false;  // receives hover/active state
  bool borderTop = false;    // 1 px `border` line along the top edge
  bool borderBottom = false;
  bool selectable = false;   // a click selects it among its siblings (segmented items)
  r1ui::platform::CursorShape cursor = r1ui::platform::CursorShape::Arrow;
  int glyph = 0;             // ChromeGlyphKind or Mode index
  uint32_t swatchRgb = 0;    // 0xRRGGBB
  CachedStyle boxCache;   // resolved `style` (state aware)
  CachedStyle textCache;  // resolved `textStyle` (always State::kNone)
};

// The preview's additions to the toolkit's built-in rule table (title bar, text roles, segmented
// control, paint field, small icon button), all expressed with token references.
std::span<const r1ui::theme::StyleRuleEntry> previewRules();

// builtinRules() followed by previewRules().
std::vector<r1ui::theme::StyleRuleEntry> allRules();

}  // namespace preview
