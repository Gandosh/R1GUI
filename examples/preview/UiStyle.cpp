// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the shell's style rows (see UiNode.h) and the Mode names.
// Why: every colour, size and font of the title bar comes from a StyleSheet row that references
//   design tokens, so a theme switch needs no code and the rows are the single place to tune a look.
//   The widget modes use the widget library's own rows.
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

    // Text roles of the title bar: colour from the Foreground row, size/weight/line height as in
    // docs/spec/widgets.md.
    {"text.app", kNone, StyleProperty::Foreground, "color:surface"},
    {"text.app", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"text.app", kNone, StyleProperty::FontWeight, "weight:semibold"},
    {"text.app", kNone, StyleProperty::LineHeight, "number:16"},
    {"text.app.muted", kNone, StyleProperty::Foreground, "color:muted"},
    {"text.app.muted", kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"text.app.muted", kNone, StyleProperty::LineHeight, "number:16"},

    // The window area behind the modes.
    {"canvas", kNone, StyleProperty::Background, "color:canvas"},
};

}  // namespace

const char* modeName(Mode mode) {
  switch (mode) {
    case Mode::Gallery: return "Widget gallery";
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
