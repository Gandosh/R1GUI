// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the preview's style rows (see UiNode.h) and the Mode names.
// Why: every colour, size and font in the preview comes from a StyleSheet row that references
//   design tokens, so a theme switch needs no code and the rows are the single place to tune a look.
// Callers: Scene.cpp (builds the StyleSheet), tests/preview.
#include <iterator>

#include "UiNode.h"

namespace preview {

namespace {

using r1ui::theme::State::kActive;
using r1ui::theme::State::kHover;
using r1ui::theme::State::kNone;
using r1ui::theme::State::kSelected;
using r1ui::theme::StyleProperty;

constexpr r1ui::theme::StyleRuleEntry kRules[] = {
    // Title bar (about 32 px) and its buttons; the close button turns red on hover like the OS one.
    {"titlebar", kNone, StyleProperty::Background, "color:panel-secondary"},
    {"titlebar", kNone, StyleProperty::BorderColor, "color:border"},
    {"chrome.button", kNone, StyleProperty::Background, "transparent"},
    {"chrome.button", kNone, StyleProperty::Foreground, "color:surface"},
    {"chrome.button", kHover, StyleProperty::Background, "color:hover"},
    {"chrome.button", kActive, StyleProperty::Background, "color:border"},
    {"chrome.close", kNone, StyleProperty::Background, "transparent"},
    {"chrome.close", kNone, StyleProperty::Foreground, "color:surface"},
    // The close button uses the OS's own red on hover and press (a literal: no token is a strong red).
    {"chrome.close", kHover, StyleProperty::Background, "#c42b1c"},
    {"chrome.close", kHover, StyleProperty::Foreground, "#ffffff"},
    {"chrome.close", kActive, StyleProperty::Background, "#a42318"},
    {"chrome.close", kActive, StyleProperty::Foreground, "#ffffff"},
    {"mode.square", kNone, StyleProperty::Background, "transparent"},
    {"mode.square", kNone, StyleProperty::BorderColor, "color:muted"},
    {"mode.square", kNone, StyleProperty::BorderWidth, "number:1"},
    {"mode.square", kSelected, StyleProperty::Background, "color:accent"},
    {"mode.square", kSelected, StyleProperty::BorderColor, "color:accent"},

    // Text roles: colour from the Foreground row, size/weight/line height as in docs/spec/widgets.md.
    {"text.app", kNone, StyleProperty::Foreground, "color:surface"},
    {"text.app", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"text.app", kNone, StyleProperty::FontWeight, "weight:semibold"},
    {"text.app", kNone, StyleProperty::LineHeight, "number:16"},
    {"text.app.muted", kNone, StyleProperty::Foreground, "color:muted"},
    {"text.app.muted", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"text.app.muted", kNone, StyleProperty::LineHeight, "number:16"},
    {"text.header", kNone, StyleProperty::Foreground, "color:surface"},
    {"text.header", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"text.header", kNone, StyleProperty::FontWeight, "weight:semibold"},
    {"text.header", kNone, StyleProperty::LineHeight, "number:16"},
    {"text.label", kNone, StyleProperty::Foreground, "color:muted"},
    {"text.label", kNone, StyleProperty::FontSize, "fontSize:11"},
    {"text.label", kNone, StyleProperty::LineHeight, "number:14"},
    {"text.value", kNone, StyleProperty::Foreground, "color:surface"},
    {"text.value", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"text.value", kNone, StyleProperty::LineHeight, "number:16"},
    {"text.muted", kNone, StyleProperty::Foreground, "color:muted"},
    {"text.muted", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"text.muted", kNone, StyleProperty::LineHeight, "number:16"},
    {"text.hex", kNone, StyleProperty::Foreground, "color:surface"},
    {"text.hex", kNone, StyleProperty::FontSize, "fontSize:11"},
    {"text.hex", kNone, StyleProperty::LineHeight, "number:14"},

    // Segmented control: field-coloured container, 22 px items, selected item on panel-selected-muted.
    {"segmented", kNone, StyleProperty::Background, "color:panel-field"},
    {"segmented", kNone, StyleProperty::Radius, "metric:field.radius"},
    {"segmented", kNone, StyleProperty::Height, "metric:field.height"},
    {"segmented.item", kNone, StyleProperty::Background, "transparent"},
    {"segmented.item", kNone, StyleProperty::Foreground, "color:muted"},
    {"segmented.item", kNone, StyleProperty::Radius, "metric:segmented.radius"},
    {"segmented.item", kNone, StyleProperty::Height, "metric:segmented.itemHeight"},
    {"segmented.item", kNone, StyleProperty::FontSize, "fontSize:11"},
    {"segmented.item", kNone, StyleProperty::LineHeight, "number:14"},
    {"segmented.item", kHover, StyleProperty::Background, "color:hover"},
    {"segmented.item", kHover, StyleProperty::Foreground, "color:surface"},
    {"segmented.item", kSelected, StyleProperty::Background, "color:panel-selected-muted"},
    {"segmented.item", kSelected, StyleProperty::Foreground, "color:surface"},

    // 20 px icon button used inside fields (the "apply variable" button, chevrons).
    {"button.ghost.sm", kNone, StyleProperty::Background, "transparent"},
    {"button.ghost.sm", kNone, StyleProperty::Foreground, "color:muted"},
    {"button.ghost.sm", kNone, StyleProperty::BorderColor, "transparent"},
    {"button.ghost.sm", kNone, StyleProperty::BorderWidth, "number:1"},
    {"button.ghost.sm", kNone, StyleProperty::Radius, "metric:iconButton.sm.radius"},
    {"button.ghost.sm", kNone, StyleProperty::Height, "metric:iconButton.sm.size"},
    {"button.ghost.sm", kHover, StyleProperty::Background, "color:hover"},
    {"button.ghost.sm", kHover, StyleProperty::Foreground, "color:surface"},

    // Paint field (swatch + hex + opacity) and the colour chip.
    {"paint.field", kNone, StyleProperty::Background, "color:input"},
    {"paint.field", kNone, StyleProperty::BorderColor, "color:border"},
    {"paint.field", kNone, StyleProperty::BorderWidth, "number:1"},
    {"paint.field", kNone, StyleProperty::Radius, "metric:field.radius"},
    {"paint.field", kNone, StyleProperty::Height, "number:28"},
    {"swatch", kNone, StyleProperty::BorderColor, "color:border"},
    {"swatch", kNone, StyleProperty::BorderWidth, "number:1"},
    {"swatch", kNone, StyleProperty::Radius, "radius:sm"},

    // The window area outside the panel.
    {"canvas", kNone, StyleProperty::Background, "color:canvas"},
};

}  // namespace

const char* modeName(Mode mode) {
  switch (mode) {
    case Mode::Widgets: return "Widgets";
    case Mode::Swatches: return "Token swatches";
    case Mode::Screens: return "Reference screens";
    case Mode::Sandbox: return "Docking sandbox";
  }
  return "?";
}

std::span<const r1ui::theme::StyleRuleEntry> previewRules() { return kRules; }

std::vector<r1ui::theme::StyleRuleEntry> allRules() {
  const auto builtin = r1ui::theme::builtinRules();
  std::vector<r1ui::theme::StyleRuleEntry> rules(builtin.begin(), builtin.end());
  rules.insert(rules.end(), std::begin(kRules), std::end(kRules));
  return rules;
}

}  // namespace preview
