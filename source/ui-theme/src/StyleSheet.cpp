// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: StyleSheet table compilation (reference parsing and validation), state-aware
//   resolution, and the built-in rule table.
// Why: see StyleSheet.h for the grammar and precedence rules.
// Callers: widgets, tests. Calls: Tokens / Theme.
#include "r1ui/theme/StyleSheet.h"

#include <charconv>
#include <cmath>

namespace r1ui::theme {

namespace {

// ---- built-in rows (the starting table for Phase 4 to extend) ----
constexpr StyleRuleEntry kBuiltin[] = {
    // Panel surface.
    {"panel.background", State::kNone, StyleProperty::Background, "color:panel"},
    {"panel.background", State::kNone, StyleProperty::Foreground, "color:surface"},

    // Section of a properties panel: header row 26 px, 11 px semibold title.
    {"panel.section", State::kNone, StyleProperty::Background, "color:panel"},
    {"panel.section", State::kNone, StyleProperty::Foreground, "color:surface"},
    {"panel.section", State::kNone, StyleProperty::BorderColor, "color:border"},
    {"panel.section", State::kNone, StyleProperty::BorderWidth, "number:1"},
    {"panel.section", State::kNone, StyleProperty::PaddingX, "space:panel-x"},
    {"panel.section", State::kNone, StyleProperty::PaddingY, "space:panel-y"},
    {"panel.section", State::kNone, StyleProperty::FontSize, "fontSize:11"},
    {"panel.section", State::kNone, StyleProperty::FontWeight, "weight:semibold"},
    {"panel.section", State::kNone, StyleProperty::Height, "metric:panelSection.headerHeight"},

    // Panel-tone field (number field, text input, select trigger).
    {"field.panel", State::kNone, StyleProperty::Background, "color:panel-field"},
    {"field.panel", State::kNone, StyleProperty::Foreground, "color:surface"},
    {"field.panel", State::kNone, StyleProperty::BorderColor, "transparent"},
    {"field.panel", State::kNone, StyleProperty::BorderWidth, "metric:field.borderWidth"},
    {"field.panel", State::kNone, StyleProperty::Radius, "metric:field.radius"},
    {"field.panel", State::kNone, StyleProperty::Height, "metric:field.height"},
    {"field.panel", State::kNone, StyleProperty::PaddingX, "metric:field.padX"},
    {"field.panel", State::kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"field.panel", State::kHover, StyleProperty::Background, "color:panel-field-hover"},
    {"field.panel", State::kFocus, StyleProperty::BorderColor, "color:panel-focus"},
    {"field.panel", State::kMixed, StyleProperty::Foreground, "color:muted"},
    {"field.panel", State::kBound, StyleProperty::Foreground, "color:component"},
    {"field.panel", State::kDisabled, StyleProperty::Opacity, "number:0.6"},

    // Ghost icon button (26 px panel size).
    {"button.ghost", State::kNone, StyleProperty::Background, "transparent"},
    {"button.ghost", State::kNone, StyleProperty::Foreground, "color:muted"},
    {"button.ghost", State::kNone, StyleProperty::BorderColor, "transparent"},
    {"button.ghost", State::kNone, StyleProperty::BorderWidth, "number:1"},
    {"button.ghost", State::kNone, StyleProperty::Radius, "metric:iconButton.md.radius"},
    {"button.ghost", State::kNone, StyleProperty::Height, "metric:iconButton.md.size"},
    {"button.ghost", State::kHover, StyleProperty::Background, "color:hover"},
    {"button.ghost", State::kHover, StyleProperty::Foreground, "color:surface"},
    {"button.ghost", State::kFocus, StyleProperty::BorderColor, "color:panel-focus"},
    {"button.ghost", State::kActive, StyleProperty::BorderColor, "color:accent"},
    {"button.ghost", State::kActive, StyleProperty::Foreground, "color:accent"},
    {"button.ghost", State::kDisabled, StyleProperty::Opacity, "number:0.5"},

    // Accent button (small).
    {"button.accent", State::kNone, StyleProperty::Background, "color:accent"},
    {"button.accent", State::kNone, StyleProperty::Foreground, "#ffffff"},
    {"button.accent", State::kNone, StyleProperty::Radius, "radius:md"},
    {"button.accent", State::kNone, StyleProperty::Height, "metric:button.sm.height"},
    {"button.accent", State::kNone, StyleProperty::PaddingX, "metric:button.sm.padX"},
    {"button.accent", State::kNone, StyleProperty::FontSize, "fontSize:xs"},
    {"button.accent", State::kNone, StyleProperty::FontWeight, "weight:medium"},
    {"button.accent", State::kHover, StyleProperty::Background, "color:accent@0.9"},
    {"button.accent", State::kDisabled, StyleProperty::Opacity, "number:0.5"},
};

bool parseDouble(std::string_view text, double& out) {
  if (text.empty()) return false;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
  return ec == std::errc() && end == text.data() + text.size() && std::isfinite(out);
}

bool isColorProperty(StyleProperty p) {
  return p == StyleProperty::Background || p == StyleProperty::Foreground || p == StyleProperty::BorderColor;
}

}  // namespace

std::span<const StyleRuleEntry> builtinRules() { return kBuiltin; }

StyleSheetResult StyleSheet::create(std::span<const StyleRuleEntry> rules, const Tokens& tokens) {
  StyleSheetResult result;
  StyleSheet sheet;
  auto fail = [&](const StyleRuleEntry& e, const std::string& why) {
    result.errors.push_back(std::string(e.key ? e.key : "(null)") + ": " + why);
  };
  for (const StyleRuleEntry& entry : rules) {
    if (entry.key == nullptr || entry.ref == nullptr || entry.key[0] == '\0') {
      fail(entry, "missing key or reference");
      continue;
    }
    Compiled c;
    c.when = entry.when;
    c.property = entry.property;
    const std::string_view ref = entry.ref;
    const size_t colon = ref.find(':');
    const std::string_view head = colon == std::string_view::npos ? ref : ref.substr(0, colon);
    const std::string_view tail = colon == std::string_view::npos ? std::string_view{} : ref.substr(colon + 1);
    bool ok = true;
    std::string why;
    if (ref == "transparent") {
      c.kind = Kind::Transparent;
    } else if (!ref.empty() && ref[0] == '#') {
      const std::optional<Color> lit = parseColor(ref);
      c.kind = Kind::Literal;
      ok = lit.has_value();
      if (ok) c.literal = *lit;
      else why = "bad colour literal '" + std::string(ref) + "'";
    } else if (head == "color") {
      c.kind = Kind::Color;
      const size_t at = tail.find('@');
      c.name = std::string(tail.substr(0, at));
      c.number = 1.0;
      if (at != std::string_view::npos && (!parseDouble(tail.substr(at + 1), c.number) || c.number < 0.0 || c.number > 1.0)) {
        ok = false;
        why = "bad alpha in '" + std::string(ref) + "'";
      } else if (!tokens.color(ThemeId::Dark, c.name) || !tokens.color(ThemeId::Light, c.name)) {
        ok = false;
        why = "unknown colour token '" + c.name + "'";
      }
    } else if (head == "space" || head == "radius" || head == "fontSize" || head == "lineHeight" || head == "weight") {
      c.name = std::string(tail);
      if (head == "space") { c.kind = Kind::Space; ok = tokens.space(c.name).has_value(); }
      else if (head == "radius") { c.kind = Kind::Radius; ok = tokens.radius(c.name).has_value(); }
      else if (head == "fontSize") { c.kind = Kind::FontSize; ok = tokens.fontSize(c.name).has_value(); }
      else if (head == "lineHeight") { c.kind = Kind::LineHeight; ok = tokens.lineHeight(c.name).has_value(); }
      else { c.kind = Kind::Weight; ok = tokens.fontWeight(c.name).has_value(); }
      if (!ok) why = "unknown token in '" + std::string(ref) + "'";
    } else if (head == "metric") {
      c.kind = Kind::Metric;
      c.name = std::string(tail);
      const size_t dot = tail.find('.');
      ok = dot != std::string_view::npos && tokens.widgetMetric(tail.substr(0, dot), tail.substr(dot + 1)).has_value();
      if (!ok) why = "unknown widget metric '" + c.name + "'";
    } else if (head == "number") {
      c.kind = Kind::Number;
      ok = parseDouble(tail, c.number);
      if (!ok) why = "bad number in '" + std::string(ref) + "'";
    } else {
      ok = false;
      why = "unrecognised reference '" + std::string(ref) + "'";
    }
    if (ok) {  // the reference must suit the property
      const bool colorRef = c.kind == Kind::Color || c.kind == Kind::Literal || c.kind == Kind::Transparent;
      bool fits = false;
      switch (entry.property) {
        case StyleProperty::Background: case StyleProperty::Foreground: case StyleProperty::BorderColor:
          fits = colorRef; break;
        case StyleProperty::Radius: fits = c.kind == Kind::Radius || c.kind == Kind::Metric || c.kind == Kind::Number; break;
        case StyleProperty::PaddingX: case StyleProperty::PaddingY: case StyleProperty::BorderWidth: case StyleProperty::Height:
          fits = c.kind == Kind::Space || c.kind == Kind::Metric || c.kind == Kind::Number; break;
        case StyleProperty::FontSize: fits = c.kind == Kind::FontSize || c.kind == Kind::Metric || c.kind == Kind::Number; break;
        case StyleProperty::LineHeight: fits = c.kind == Kind::LineHeight || c.kind == Kind::Number; break;
        case StyleProperty::FontWeight: fits = c.kind == Kind::Weight || c.kind == Kind::Number; break;
        case StyleProperty::Opacity: fits = c.kind == Kind::Number && c.number >= 0.0 && c.number <= 1.0; break;
      }
      if (!fits) {
        ok = false;
        why = "reference '" + std::string(ref) + "' does not suit the property";
      }
    }
    if (!ok) {
      fail(entry, why);
      continue;
    }
    sheet.rules_[entry.key].push_back(std::move(c));
  }
  if (result.errors.empty()) result.sheet = std::move(sheet);
  return result;
}

std::optional<ResolvedStyle> StyleSheet::resolve(const Theme& theme, std::string_view key, uint8_t state) const {
  const auto it = rules_.find(std::string(key));
  if (it == rules_.end()) return std::nullopt;
  const Tokens& tokens = theme.tokens();
  uint8_t mask = state;
  if ((mask & State::kDisabled) != 0) mask = static_cast<uint8_t>(mask & ~(State::kHover | State::kActive));

  ResolvedStyle r;
  r.text.fontSize = tokens.bodyFontSize().value_or(13.0);
  std::optional<LineHeight> lineHeight;
  for (const Compiled& c : it->second) {
    if ((c.when & mask) != c.when) continue;
    if (isColorProperty(c.property)) {
      Color color{0, 0, 0, 0};
      if (c.kind == Kind::Color) {
        color = theme.color(c.name).value_or(Color{0, 0, 0, 0});
        color.a = static_cast<uint8_t>(std::lround(color.a * c.number));
      } else if (c.kind == Kind::Literal) {
        color = c.literal;
      }
      if (c.property == StyleProperty::Background) r.background = color;
      else if (c.property == StyleProperty::Foreground) r.text.color = color;
      else r.border.color = color;
      continue;
    }
    double v = c.number;
    switch (c.kind) {
      case Kind::Space: v = tokens.space(c.name).value_or(0.0); break;
      case Kind::Radius: v = tokens.radius(c.name).value_or(0.0); break;
      case Kind::FontSize: v = tokens.fontSize(c.name).value_or(r.text.fontSize); break;
      case Kind::Weight: v = tokens.fontWeight(c.name).value_or(400); break;
      case Kind::Metric: {
        const size_t dot = c.name.find('.');
        v = tokens.widgetMetric(std::string_view(c.name).substr(0, dot), std::string_view(c.name).substr(dot + 1)).value_or(0.0);
        break;
      }
      case Kind::LineHeight: lineHeight = tokens.lineHeight(c.name); break;
      default: break;
    }
    switch (c.property) {
      case StyleProperty::BorderWidth: r.border.width = v; break;
      case StyleProperty::Radius: r.radius = v; break;
      case StyleProperty::PaddingX: r.paddingX = v; break;
      case StyleProperty::PaddingY: r.paddingY = v; break;
      case StyleProperty::FontSize: r.text.fontSize = v; break;
      case StyleProperty::LineHeight:
        if (c.kind == Kind::Number) lineHeight = LineHeight{v, false};
        break;
      case StyleProperty::FontWeight: r.text.weight = static_cast<int>(v); break;
      case StyleProperty::Opacity: r.opacity = v; break;
      case StyleProperty::Height: r.height = v; break;
      default: break;
    }
  }
  r.text.lineHeight = lineHeight ? lineHeight->resolve(r.text.fontSize) : 1.25 * r.text.fontSize;
  return r;
}

}  // namespace r1ui::theme
