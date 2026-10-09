// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the per-widget payload of the preview's own small widget tree (the title bar: what a node
//   looks like and how it reacts), the preview mode enumeration and the preview-specific style rows.
// Why: the shell's title bar is drawn by the Phase 3 tree (ui-core tree + router + invalidator) so the
//   caption buttons keep the borderless window's behaviour; everything below it is the real widget
//   library. The tree's tree::Widget carries layout, flags and one user slot; the shell keeps the look
//   (style key, text, state bits) in a side table indexed by that slot so painting and measuring stay
//   data driven and the style table stays the only place colours come from.
// Callers: Scene*.cpp, PreviewApp.cpp, tests/preview. Calls: ui-theme (StyleSheet rows).
// Invariants: Node::userIndex is the widget's userData; a node is only reachable through a live
//   widget id; `state` holds only theme::State bits owned by the widget (hover, active, selected).
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
enum class Mode : uint8_t { Editor, Gallery, Widgets, Swatches, Screens, Sandbox };
inline constexpr int kModeCount = 6;
const char* modeName(Mode mode);
// True for the modes that are the real widget library in a UiContext (the others draw directly).
inline bool isWidgetMode(Mode mode) { return mode == Mode::Editor || mode == Mode::Gallery || mode == Mode::Widgets; }

enum class NodeKind : uint8_t {
  Box,         // background/border from `style`, optional separator lines
  Text,        // one line of text styled by `textStyle`, measured by the text module
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
  std::string textStyle;  // StyleSheet key for text colour and size
  std::string text;
  uint8_t state = 0;      // theme::State bits set by pointer events
  bool interactive = false;  // receives hover/active state
  bool borderBottom = false;
  int glyph = 0;             // ChromeGlyphKind or Mode index
  CachedStyle boxCache;   // resolved `style` (state aware)
  CachedStyle textCache;  // resolved `textStyle` (always State::kNone)
};

// The shell's additions to the toolkit's built-in rule table (title bar, text roles), all expressed
// with token references.
std::span<const r1ui::theme::StyleRuleEntry> previewRules();

// builtinRules() followed by previewRules().
std::vector<r1ui::theme::StyleRuleEntry> allRules();

}  // namespace preview
