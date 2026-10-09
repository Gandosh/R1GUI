// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for the non-colour token sections of theme::Tokens: exact values from the real
//   assets/theme/tokens.json, and hostile token files (missing sections, wrong types, negative
//   sizes, overflowing numbers, unknown keys).
// Callers: CTest (label fast); argv[1] is the path of the real tokens.json.
#include <cstdio>
#include <string>

#include "r1ui/theme/Tokens.h"

using namespace r1ui::theme;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

const std::string kThemes = R"("themes":{"dark":{"a":"#000000"},"light":{"a":"#ffffff"}})";

std::string file(const std::string& sections) { return "{" + kThemes + (sections.empty() ? "" : "," + sections) + "}"; }

void expectRejected(const std::string& sections, const char* what, const ParseOptions& options = {}) {
  const TokensResult r = Tokens::parse(file(sections), options);
  expect(!r.ok() && !r.error.empty(), what);
}

void expectAccepted(const std::string& sections, const char* what) {
  const TokensResult r = Tokens::parse(file(sections));
  if (!r.ok()) std::fprintf(stderr, "  error: %s\n", r.error.c_str());
  expect(r.ok(), what);
}

void realFile(const std::string& path) {
  ParseOptions strict;
  strict.requireAllSections = true;
  const TokensResult r = Tokens::loadFile(path, strict);
  expect(r.ok(), "real tokens.json satisfies requireAllSections");
  if (!r.ok()) {
    std::fprintf(stderr, "  error: %s\n", r.error.c_str());
    return;
  }
  const Tokens& t = *r.tokens;
  for (const Section s : {Section::Space, Section::Radius, Section::Font, Section::Shadow, Section::Motion, Section::Widget}) {
    expect(t.hasSection(s), "section present");
  }
  expect(t.space("control") == 26.0 && t.space("panel-x") == 12.0 && t.space("panel-rail") == 26.0, "space tokens");
  expect(t.space("unit") == 4.0 && !t.space("nope").has_value(), "space unit and unknown name");
  expect(t.radius("panel") == 4.0 && t.radius("md") == 6.0 && t.radius("full") == 9999.0, "radius tokens");
  expect(t.fontSize("xs") == 12.0 && t.fontSize("13") == 13.0 && t.fontSize("2xl") == 24.0, "font sizes");
  expect(t.bodyFontSize() == 13.0 && t.fontFamily() == "Inter", "body size and family");
  expect(t.fontFallback().size() == 2 && t.fontFallback()[0] == "system-ui", "fallback list");
  const auto xs = t.lineHeight("xs");
  expect(xs && !xs->relative && xs->value == 16.0 && xs->resolve(12.0) == 16.0, "pixel line height");
  const auto tight = t.lineHeight("tight");
  expect(tight && tight->relative && tight->resolve(12.0) == 15.0, "ratio line height");
  expect(t.fontWeight("semibold") == 600 && t.fontWeight("normal") == 400, "weights");
  expect(t.fontFile(500) == "Inter-Medium.ttf" && !t.fontFile(550).has_value(), "font files");
  expect(t.letterSpacing("wide") == 0.025, "tracking");

  const auto lg = t.shadow("lg");
  expect(lg && lg->size() == 2, "shadow lg has two layers");
  if (lg && lg->size() == 2) {
    expect((*lg)[0] == ShadowLayer{0, 10, 15, -3, Color{0, 0, 0, 0x1a}}, "lg layer 0 = 0 10 15 -3 #0000001a");
    expect((*lg)[1] == ShadowLayer{0, 4, 6, -4, Color{0, 0, 0, 0x1a}}, "lg layer 1 = 0 4 6 -4 #0000001a");
  }
  const auto xxl = t.shadow("2xl");
  expect(xxl && xxl->size() == 1 && (*xxl)[0] == ShadowLayer{0, 25, 50, -12, Color{0, 0, 0, 0x40}}, "2xl");
  const auto overlay = t.shadow("overlay");
  expect(overlay && (*overlay)[0] == ShadowLayer{0, 8, 30, 0, Color{0, 0, 0, 0x66}}, "overlay");
  expect(!t.shadow("_format").has_value() && !t.shadow("nope").has_value(), "metadata and unknown shadows are absent");

  expect(t.motionDuration() == 0.15, "motion duration");
  const auto easing = t.motionEasing();
  expect(easing && (*easing)[0] == 0.4 && (*easing)[1] == 0.0 && (*easing)[2] == 0.2 && (*easing)[3] == 1.0, "easing");
  expect(t.motionValue("blurXl") == 24.0, "extra motion value");

  expect(t.widgetMetric("field", "height") == 26.0 && t.widgetMetric("field", "radius") == 4.0, "field metrics");
  expect(t.widgetMetric("button", "sm.height") == 28.0 && t.widgetMetric("button", "icon") == 32.0, "nested metrics");
  expect(t.widgetMetric("iconButton", "md.size") == 26.0 && t.widgetMetric("panelSection", "headerHeight") == 26.0,
         "more metrics");
  expect(t.widgetMetricText("iconButton", "sm.text") == "sm", "text metric");
  expect(!t.widgetMetric("field", "nope").has_value() && !t.widgetMetric("_source", "x").has_value(), "unknown metric");
  expect(t.colorCount() == 34, "colours unaffected");
}

void missingSections() {
  const TokensResult bare = Tokens::parse(file(""));
  expect(bare.ok(), "a file with only themes is accepted by default");
  if (bare.ok()) {
    const Tokens& t = *bare.tokens;
    expect(!t.hasSection(Section::Space) && !t.space("control") && !t.shadow("lg") && !t.motionDuration() &&
               !t.widgetMetric("field", "height") && !t.bodyFontSize() && t.fontFamily().empty(),
           "absent sections answer nullopt / empty");
  }
  ParseOptions strict;
  strict.requireAllSections = true;
  expectRejected("", "requireAllSections rejects a themes-only file", strict);
  expectRejected(R"("space":{},"radius":{},"font":{},"shadow":{},"motion":{})", "requireAllSections rejects a missing widget section", strict);
  expect(!Tokens::parse(R"({"space":{}})").ok(), "themes are still mandatory");
}

void wrongTypes() {
  expectRejected(R"("space":[1,2])", "section must be an object");
  expectRejected(R"("space":{"a":"4"})", "string where a number is expected");
  expectRejected(R"("space":{"a":null})", "null space value");
  expectRejected(R"("radius":{"a":true})", "bool radius value");
  expectRejected(R"("font":{"family":""})", "empty family");
  expectRejected(R"("font":{"family":3})", "family not a string");
  expectRejected(R"("font":{"fallback":"x"})", "fallback not an array");
  expectRejected(R"("font":{"fallback":[1]})", "fallback entry not a string");
  expectRejected(R"("font":{"files":{"abc":"x.ttf"}})", "non-numeric weight key");
  expectRejected(R"("font":{"files":{"0":"x.ttf"}})", "weight key out of range");
  expectRejected(R"("font":{"files":{"400":5}})", "file name not a string");
  expectRejected(R"("font":{"size":[1]})", "size not an object");
  expectRejected(R"("font":{"weight":{"x":450.5}})", "fractional weight");
  expectRejected(R"("font":{"weight":{"x":1001}})", "weight above 1000");
  expectRejected(R"("shadow":{"a":5})", "shadow not an array");
  expectRejected(R"("shadow":{"a":[[0,1,2,3]]})", "layer with four components");
  expectRejected(R"("shadow":{"a":[[0,1,"2",3,"#000000"]]})", "non-numeric layer component");
  expectRejected(R"("shadow":{"a":[[0,1,2,3,"red"]]})", "bad shadow colour");
  expectRejected(R"("shadow":{"a":[[0,1,2,3,7]]})", "non-string shadow colour");
  expectRejected(R"("motion":{"easing":[0,0,1]})", "easing with three numbers");
  expectRejected(R"("motion":{"easing":[2,0,0.2,1]})", "easing x out of range");
  expectRejected(R"("motion":{"duration":"fast"})", "duration not a number");
  expectRejected(R"("widget":{"a":true})", "bool metric");
  expectRejected(R"("widget":{"a":[1]})", "array metric");
  expectRejected(R"("widget":{"a":null})", "null metric");
}

void badNumbers() {
  expectRejected(R"("space":{"a":-1})", "negative space");
  expectRejected(R"("radius":{"a":-0.5})", "negative radius");
  expectRejected(R"("font":{"size":{"a":0}})", "zero font size");
  expectRejected(R"("font":{"size":{"a":-12}})", "negative font size");
  expectRejected(R"("font":{"lineHeight":{"a":0}})", "zero line height");
  expectRejected(R"("font":{"bodySize":-1})", "negative body size");
  expectRejected(R"("font":{"weight":{"x":0}})", "zero weight");
  expectRejected(R"("shadow":{"a":[[0,1,-2,3,"#000000"]]})", "negative blur");
  expectRejected(R"("motion":{"duration":-0.1})", "negative duration");
  expectRejected(R"("widget":{"field":{"height":-26}})", "negative widget metric");
  expect(!Tokens::parse(file(R"("space":{"a":NaN})")).ok(), "NaN literal is not JSON");
  expect(!Tokens::parse(file(R"("space":{"a":Infinity})")).ok(), "Infinity literal is not JSON");
  expect(!Tokens::parse(file(R"("space":{"a":1e999})")).ok(), "overflowing number is rejected");
  expect(!Tokens::parse(file(R"("space":{"a":-1e999})")).ok(), "negative overflow is rejected");
  expectAccepted(R"("space":{"a":0,"b":1e9})", "zero and very large sizes are valid");
  std::string deep = R"("widget":)";
  for (int i = 0; i < 12; ++i) deep += R"({"a":)";
  deep += "1";
  for (int i = 0; i < 12; ++i) deep += "}";
  expectRejected(deep, "metrics nested deeper than the limit");
  std::string many = R"("shadow":{"a":[)";
  for (int i = 0; i < 17; ++i) many += std::string(i ? "," : "") + R"([0,1,2,3,"#000000"])";
  many += "]}";
  expectRejected(many, "too many shadow layers");
}

void unknownKeysAndMetadata() {
  expectAccepted(R"("space":{"a":1,"_note":"free text"},"zzz":{"anything":[1,2,3]},"extra":5)", "unknown keys are ignored");
  expectAccepted(R"("font":{"mystery":true,"size":{"a":12}},"motion":{"note":"hello","duration":0.2})", "unknown keys inside sections");
  expectAccepted(R"("shadow":{"_format":"doc text","s":[[0,1,2,3,"#00000080"]]},"widget":{"_source":"x","b":{"h":3}})", "metadata keys");
  const TokensResult r = Tokens::parse(file(R"("shadow":{"_format":"x","s":[]},"widget":{"b":{"h":3,"t":"txt"}},"motion":{"note":1})"));
  expect(r.ok() && r.tokens->shadow("s")->empty(), "an empty shadow list is a valid 'no shadow'");
  expect(r.ok() && r.tokens->widgetMetric("b", "h") == 3.0 && r.tokens->widgetMetricText("b", "t") == "txt", "metrics flatten");
  expect(r.ok() && r.tokens->motionValue("note") == 1.0, "other numeric motion keys are kept");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: token_sections_test <tokens.json>\n");
    return 2;
  }
  realFile(argv[1]);
  missingSections();
  wrongTypes();
  badNumbers();
  unknownKeysAndMetadata();
  if (failures != 0) std::fprintf(stderr, "ui-theme.token_sections: %d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
