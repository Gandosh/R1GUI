// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for theme::Theme and theme::StyleSheet: exact resolved styles from the real
//   tokens.json for the built-in rows in both themes and every state, theme switching without a
//   reload, state precedence, and rejection of damaged rule tables.
// Callers: CTest (label fast); argv[1] is the path of the real tokens.json.
#include <cstdio>
#include <memory>
#include <vector>

#include "r1ui/theme/StyleSheet.h"

using namespace r1ui::theme;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

const Color kClear{0, 0, 0, 0};

void builtinRows(const std::string& path) {
  const TokensResult loaded = Tokens::loadFile(path);
  if (!loaded.ok()) {
    std::fprintf(stderr, "FAIL: cannot load tokens: %s\n", loaded.error.c_str());
    ++failures;
    return;
  }
  const auto tokens = std::make_shared<const Tokens>(*loaded.tokens);
  StyleSheetResult created = StyleSheet::create(builtinRules(), *tokens);
  for (const auto& e : created.errors) std::fprintf(stderr, "  rule error: %s\n", e.c_str());
  expect(created.ok(), "built-in table compiles against the real tokens");
  if (!created.ok()) return;
  const StyleSheet& sheet = *created.sheet;
  Theme theme(tokens);

  expect(sheet.keyCount() == 5 && sheet.hasKey("button.ghost") && !sheet.hasKey("button.nope"), "keys");
  expect(!sheet.resolve(theme, "no.such.key", State::kNone).has_value(), "unknown key resolves to nothing");

  // panel.background in both themes, switched without reloading.
  const auto dark = sheet.resolve(theme, "panel.background", State::kNone);
  expect(dark && dark->background == Color{0x2a, 0x2a, 0x2a, 255} && dark->text.color == Color{0xe0, 0xe0, 0xe0, 255},
         "dark panel is #2a2a2a with #e0e0e0 text");
  const uint32_t before = theme.revision();
  theme.set(ThemeId::Light);
  expect(theme.revision() == before + 1, "an effective switch bumps the revision");
  theme.set(ThemeId::Light);
  expect(theme.revision() == before + 1, "setting the same theme changes nothing");
  const auto light = sheet.resolve(theme, "panel.background", State::kNone);
  expect(light && light->background == Color{255, 255, 255, 255} && light->text.color == Color{0x20, 0x21, 0x24, 255},
         "light panel is #ffffff with #202124 text");
  theme.toggle();
  expect(theme.id() == ThemeId::Dark && theme.revision() == before + 2, "toggle returns to dark");

  // panel.section.
  const auto section = sheet.resolve(theme, "panel.section", State::kNone);
  expect(section && section->height == 26.0 && section->paddingX == 12.0 && section->paddingY == 8.0 &&
             section->text.fontSize == 11.0 && section->text.weight == 600 && section->border.width == 1.0 &&
             section->border.color == Color{0x3a, 0x3a, 0x3a, 255},
         "panel.section metrics");

  // field.panel across states (dark).
  const auto field = sheet.resolve(theme, "field.panel", State::kNone);
  expect(field && field->background == Color{0x33, 0x33, 0x33, 255} && field->border.color == kClear &&
             field->border.width == 1.0 && field->radius == 4.0 && field->height == 26.0 && field->paddingX == 6.0 &&
             field->text.fontSize == 12.0 && field->text.lineHeight == 15.0 && field->opacity == 1.0,
         "field.panel idle");
  expect(sheet.resolve(theme, "field.panel", State::kHover)->background == Color{0x3b, 0x3b, 0x3b, 255}, "field hover");
  expect(sheet.resolve(theme, "field.panel", State::kFocus)->border.color == Color{0x4c, 0x8d, 0xff, 255}, "field focus border");
  expect(sheet.resolve(theme, "field.panel", State::kMixed)->text.color == Color{0x88, 0x88, 0x88, 255}, "mixed value is muted");
  expect(sheet.resolve(theme, "field.panel", State::kBound)->text.color == Color{0x97, 0x47, 0xff, 255}, "bound value uses the component colour");
  expect(sheet.resolve(theme, "field.panel", State::kMixed | State::kBound)->text.color == Color{0x97, 0x47, 0xff, 255},
         "later rows win when states overlap");
  expect(sheet.resolve(theme, "field.panel", State::kDisabled)->opacity == 0.6, "disabled field opacity");
  expect(sheet.resolve(theme, "field.panel", State::kDisabled | State::kHover)->background == Color{0x33, 0x33, 0x33, 255},
         "a disabled field shows no hover");
  theme.set(ThemeId::Light);
  expect(sheet.resolve(theme, "field.panel", State::kNone)->background == Color{0xf0, 0xf1, 0xf3, 255} &&
             sheet.resolve(theme, "field.panel", State::kHover)->background == Color{0xe7, 0xe9, 0xed, 255},
         "light field and hover");
  theme.set(ThemeId::Dark);

  // button.ghost.
  const auto ghost = sheet.resolve(theme, "button.ghost", State::kNone);
  expect(ghost && ghost->background == kClear && ghost->text.color == Color{0x88, 0x88, 0x88, 255} && ghost->radius == 4.0 &&
             ghost->height == 26.0,
         "ghost idle: transparent, muted glyph, 26 px, radius 4");
  const auto gHover = sheet.resolve(theme, "button.ghost", State::kHover);
  expect(gHover->background == Color{0x35, 0x35, 0x35, 255} && gHover->text.color == Color{0xe0, 0xe0, 0xe0, 255}, "ghost hover");
  const auto gActive = sheet.resolve(theme, "button.ghost", State::kActive);
  expect(gActive->border.color == Color{0x3b, 0x82, 0xf6, 255} && gActive->text.color == gActive->border.color, "ghost active");
  expect(sheet.resolve(theme, "button.ghost", State::kDisabled | State::kHover | State::kActive)->background == kClear &&
             sheet.resolve(theme, "button.ghost", State::kDisabled)->opacity == 0.5,
         "disabled ghost: no hover or pressed look, half opacity");
  expect(sheet.resolve(theme, "button.ghost", State::kFocus)->border.color == Color{0x4c, 0x8d, 0xff, 255}, "ghost focus ring");
  expect(sheet.resolve(theme, "button.ghost", State::kSelected)->background == kClear, "unlisted states change nothing");

  // button.accent: alpha multiplier and literal colour.
  const auto accent = sheet.resolve(theme, "button.accent", State::kNone);
  expect(accent && accent->background == Color{0x3b, 0x82, 0xf6, 255} && accent->text.color == Color{255, 255, 255, 255} &&
             accent->radius == 6.0 && accent->height == 28.0 && accent->paddingX == 8.0 && accent->text.weight == 500,
         "accent button");
  expect(sheet.resolve(theme, "button.accent", State::kHover)->background == Color{0x3b, 0x82, 0xf6, 230}, "accent hover is 90% alpha");
}

void rejectedTables(const std::string& path) {
  const TokensResult loaded = Tokens::loadFile(path);
  if (!loaded.ok()) return;
  const Tokens& tokens = *loaded.tokens;
  const std::vector<StyleRuleEntry> bad = {
      {"x.a", State::kNone, StyleProperty::Background, "color:no-such-colour"},
      {"x.b", State::kNone, StyleProperty::Background, "color:panel@2"},
      {"x.c", State::kNone, StyleProperty::Background, "color:panel@abc"},
      {"x.d", State::kNone, StyleProperty::Opacity, "color:panel"},
      {"x.e", State::kNone, StyleProperty::Height, "metric:field.nope"},
      {"x.f", State::kNone, StyleProperty::Height, "metric:nodot"},
      {"x.g", State::kNone, StyleProperty::Background, "#12345"},
      {"x.h", State::kNone, StyleProperty::PaddingX, "space:nope"},
      {"x.i", State::kNone, StyleProperty::Opacity, "number:1.5"},
      {"x.j", State::kNone, StyleProperty::Radius, "bogus:thing"},
      {"x.k", State::kNone, StyleProperty::Foreground, "number:3"},
      {nullptr, State::kNone, StyleProperty::Foreground, "color:panel"},
      {"x.l", State::kNone, StyleProperty::Foreground, nullptr},
      {"ok.row", State::kNone, StyleProperty::Foreground, "color:panel"},
  };
  const StyleSheetResult result = StyleSheet::create(bad, tokens);
  expect(!result.ok(), "a table with bad rows is rejected as a whole");
  expect(result.errors.size() == bad.size() - 1, "every bad row is reported, the good one is not");

  // Extending the built-in table: Phase 4 rows are validated like the shipped ones.
  std::vector<StyleRuleEntry> extended(builtinRules().begin(), builtinRules().end());
  extended.push_back({"tooltip.base", State::kNone, StyleProperty::Background, "color:panel"});
  extended.push_back({"tooltip.base", State::kNone, StyleProperty::Radius, "metric:tooltip.radius"});
  extended.push_back({"tooltip.base", State::kNone, StyleProperty::LineHeight, "lineHeight:tight"});
  extended.push_back({"tooltip.base", State::kNone, StyleProperty::FontSize, "fontSize:sm"});
  const StyleSheetResult ok = StyleSheet::create(extended, tokens);
  expect(ok.ok() && ok.sheet->keyCount() == 6, "extended table compiles");
  if (ok.ok()) {
    Theme theme(std::make_shared<const Tokens>(tokens));
    const auto tip = ok.sheet->resolve(theme, "tooltip.base", State::kNone);
    expect(tip && tip->radius == 6.0 && tip->text.fontSize == 14.0 && tip->text.lineHeight == 17.5, "ratio line height follows the font size");
  }
  expect(StyleSheet::create({}, tokens).ok(), "an empty table is valid");
  try {
    Theme broken(nullptr);
    expect(false, "Theme without tokens must throw");
  } catch (const std::invalid_argument&) {
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: style_sheet_test <tokens.json>\n");
    return 2;
  }
  builtinRows(argv[1]);
  rejectedTables(argv[1]);
  if (failures != 0) std::fprintf(stderr, "ui-theme.style_sheet: %d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
