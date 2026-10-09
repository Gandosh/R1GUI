// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the data-driven mapping from a widget style key (button.ghost, field.panel,
//   panel.section) plus state flags to a ResolvedStyle built from design tokens.
// Why: widgets must not hard-code colours or sizes; Phase 4 adds widgets by adding rows to a
//   table, not code. Rows reference tokens by name, so a theme switch changes the result with no
//   rebuild and a damaged table is rejected up front instead of at paint time.
// Callers: widgets and the preview call resolve(); Phase 4 extends builtinRules() or passes its
//   own rows to StyleSheet::create. Calls: Theme / Tokens.
// Rule table: each StyleRuleEntry sets one property of one style key, optionally only when all
//   bits of `when` are set in the widget state. Entries for a key apply in table order, later
//   entries win, so put the base value first and state overrides after it. When Disabled is set
//   the Hover and Active bits are ignored (a disabled control shows no hover or pressed look).
// Reference grammar (the `ref` text), checked against the property when the sheet is created:
//   color:NAME[@alpha]   theme colour token, optional alpha multiplier 0..1
//   #rrggbb[aa]          literal colour          transparent   fully transparent colour
//   space:NAME  radius:NAME  fontSize:NAME  lineHeight:NAME  weight:NAME
//   metric:WIDGET.KEY    widget metric (for example metric:field.height)
//   number:VALUE         literal number
// Property/reference compatibility: Background, Foreground, BorderColor take colours; Radius
//   takes radius: or metric:; PaddingX/PaddingY/BorderWidth/Height take space: or metric: or
//   number:; FontSize takes fontSize: or metric:; LineHeight takes lineHeight: or number:;
//   FontWeight takes weight: or number:; Opacity takes number:.
// Resolution defaults: transparent colours, zero sizes, opacity 1, body font size, weight 400,
//   line height 1.25 x font size unless a rule sets it.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "r1ui/theme/Theme.h"

namespace r1ui::theme {

namespace State {
inline constexpr uint8_t kNone = 0;
inline constexpr uint8_t kHover = 1;
inline constexpr uint8_t kActive = 2;
inline constexpr uint8_t kFocus = 4;
inline constexpr uint8_t kDisabled = 8;
inline constexpr uint8_t kSelected = 16;
inline constexpr uint8_t kMixed = 32;
inline constexpr uint8_t kBound = 64;
inline constexpr uint8_t kInvalid = 128;
}  // namespace State

enum class StyleProperty : uint8_t {
  Background,
  Foreground,
  BorderColor,
  BorderWidth,
  Radius,
  PaddingX,
  PaddingY,
  FontSize,
  LineHeight,
  FontWeight,
  Opacity,
  Height
};

struct StyleRuleEntry {
  const char* key;
  uint8_t when;  // State bits that must all be set; State::kNone = always
  StyleProperty property;
  const char* ref;
};

struct BorderStyle {
  double width = 0.0;
  Color color{0, 0, 0, 0};
};

struct TextStyle {
  double fontSize = 13.0;
  double lineHeight = 16.25;  // pixels
  int weight = 400;
  Color color{0, 0, 0, 0};
};

struct ResolvedStyle {
  Color background{0, 0, 0, 0};
  BorderStyle border;
  double radius = 0.0;
  double paddingX = 0.0;
  double paddingY = 0.0;
  TextStyle text;
  double opacity = 1.0;
  double height = 0.0;  // 0 = not specified by the rules
};

class StyleSheet;

struct StyleSheetResult;

// The rows shipped with the toolkit (panel.background, panel.section, field.panel, button.ghost,
// button.accent). Phase 4 appends its own rows to a copy of this table.
std::span<const StyleRuleEntry> builtinRules();

class StyleSheet {
 public:
  // Validates every row against `tokens`; any bad row rejects the whole table.
  static StyleSheetResult create(std::span<const StyleRuleEntry> rules, const Tokens& tokens);

  bool hasKey(std::string_view key) const { return rules_.count(std::string(key)) != 0; }
  size_t keyCount() const { return rules_.size(); }
  // std::nullopt for an unknown key. Reads colours from the theme's active column.
  std::optional<ResolvedStyle> resolve(const Theme& theme, std::string_view key, uint8_t state) const;

 private:
  enum class Kind : uint8_t { Color, Transparent, Literal, Space, Radius, FontSize, LineHeight, Weight, Metric, Number };
  struct Compiled {
    uint8_t when = 0;
    StyleProperty property = StyleProperty::Background;
    Kind kind = Kind::Number;
    std::string name;     // token / literal text
    double number = 0.0;  // alpha for colours, value for Number
    Color literal;
  };
  std::unordered_map<std::string, std::vector<Compiled>> rules_;
};

struct StyleSheetResult {
  std::optional<StyleSheet> sheet;
  std::vector<std::string> errors;  // empty on success; one message per bad entry
  bool ok() const { return sheet.has_value(); }
};

}  // namespace r1ui::theme
