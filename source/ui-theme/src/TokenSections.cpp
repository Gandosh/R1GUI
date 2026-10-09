// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: validation and storage of the theme-independent token sections (space, radius, font,
//   shadow, motion, widget metrics) and the typed accessors that read them.
// Why: see Tokens.h. Every value is checked here (type, finiteness, sign, range) so widgets can
//   use tokens without re-validating; nothing is stored unless the whole file is accepted
//   (Tokens::parse builds into a local object).
// Callers: Tokens::parse. Calls: r1ui::core::JsonValue.
#include <charconv>
#include <cmath>
#include <utility>

#include "r1ui/core/Json.h"
#include "r1ui/theme/Tokens.h"

namespace r1ui::theme {

namespace {

constexpr size_t kMaxShadowLayers = 16;
constexpr int kMaxMetricDepth = 8;

bool isMeta(const std::string& key) { return !key.empty() && key[0] == '_'; }

bool isNumber(const core::JsonValue& v) { return v.type() == core::JsonType::Number && std::isfinite(v.numberValue()); }

// Reads an object of numbers into `out`, requiring lo <= value (and > lo when strict).
std::string numberMap(const core::JsonValue& obj, const std::string& path, double lo, bool strict,
                      std::unordered_map<std::string, double>& out) {
  for (size_t i = 0; i < obj.size(); ++i) {
    const std::string& key = obj.keyAt(i);
    if (isMeta(key)) continue;
    const core::JsonValue& v = obj.child(i);
    if (!isNumber(v)) return path + "." + key + " is not a finite number";
    const double d = v.numberValue();
    if (d < lo || (strict && d == lo)) return path + "." + key + " is out of range";
    out[key] = d;
  }
  return {};
}

}  // namespace

std::optional<LineHeight> Tokens::lineHeight(std::string_view name) const { return lookup(lineHeight_, name); }
std::optional<int> Tokens::fontWeight(std::string_view name) const { return lookup(weight_, name); }

std::optional<std::string> Tokens::fontFile(int weight) const {
  const auto it = fontFiles_.find(weight);
  if (it == fontFiles_.end()) return std::nullopt;
  return it->second;
}

std::optional<std::vector<ShadowLayer>> Tokens::shadow(std::string_view name) const {
  return lookup(shadows_, name);
}

std::optional<double> Tokens::widgetMetric(std::string_view widget, std::string_view key) const {
  return lookup(metrics_, std::string(widget) + "." + std::string(key));
}

std::optional<std::string> Tokens::widgetMetricText(std::string_view widget, std::string_view key) const {
  return lookup(metricText_, std::string(widget) + "." + std::string(key));
}

namespace {

std::string parseShadows(const core::JsonValue& obj, std::unordered_map<std::string, std::vector<ShadowLayer>>& out) {
  for (size_t i = 0; i < obj.size(); ++i) {
    const std::string& key = obj.keyAt(i);
    if (isMeta(key)) continue;
    const std::string path = "shadow." + key;
    const core::JsonValue& list = obj.child(i);
    if (list.type() != core::JsonType::Array) return path + " is not an array of layers";
    if (list.size() > kMaxShadowLayers) return path + " has too many layers";
    std::vector<ShadowLayer> layers;
    for (size_t j = 0; j < list.size(); ++j) {
      const core::JsonValue& layer = list.child(j);
      const std::string where = path + "[" + std::to_string(j) + "]";
      if (layer.type() != core::JsonType::Array || layer.size() != 5) return where + " is not [x, y, blur, spread, colour]";
      double n[4];
      for (size_t k = 0; k < 4; ++k) {
        if (!isNumber(layer.child(k))) return where + " has a non-numeric component";
        n[k] = layer.child(k).numberValue();
      }
      if (n[2] < 0.0) return where + " has a negative blur";
      if (!layer.child(4).isString()) return where + " colour is not a string";
      const std::optional<Color> color = parseColor(layer.child(4).stringValue());
      if (!color) return where + " colour is not #rrggbb or #rrggbbaa";
      layers.push_back(ShadowLayer{n[0], n[1], n[2], n[3], *color});
    }
    out[key] = std::move(layers);
  }
  return {};
}

// Flattens widget metrics to "widget.sub.key" -> number / text. Numbers must be finite and >= 0.
std::string flattenMetrics(const core::JsonValue& obj, const std::string& prefix, int depth,
                           std::unordered_map<std::string, double>& nums,
                           std::unordered_map<std::string, std::string>& texts) {
  if (depth > kMaxMetricDepth) return "widget." + prefix + " is nested too deeply";
  for (size_t i = 0; i < obj.size(); ++i) {
    const std::string& key = obj.keyAt(i);
    if (isMeta(key)) continue;
    const std::string path = prefix.empty() ? key : prefix + "." + key;
    const core::JsonValue& v = obj.child(i);
    if (v.isObject()) {
      if (std::string e = flattenMetrics(v, path, depth + 1, nums, texts); !e.empty()) return e;
    } else if (v.isString()) {
      texts[path] = v.stringValue();
    } else if (isNumber(v)) {
      if (v.numberValue() < 0.0) return "widget." + path + " is negative";
      nums[path] = v.numberValue();
    } else {
      return "widget." + path + " is not a number, string or object";
    }
  }
  return {};
}

std::string parseMotion(const core::JsonValue& obj, std::optional<double>& duration,
                        std::optional<std::array<double, 4>>& easing, std::unordered_map<std::string, double>& values) {
  for (size_t i = 0; i < obj.size(); ++i) {
    const std::string& key = obj.keyAt(i);
    if (isMeta(key)) continue;
    const core::JsonValue& v = obj.child(i);
    if (key == "easing") {
      if (v.type() != core::JsonType::Array || v.size() != 4) return "motion.easing is not four numbers";
      std::array<double, 4> p{};
      for (size_t k = 0; k < 4; ++k) {
        if (!isNumber(v.child(k))) return "motion.easing has a non-numeric component";
        p[k] = v.child(k).numberValue();
      }
      if (p[0] < 0.0 || p[0] > 1.0 || p[2] < 0.0 || p[2] > 1.0) return "motion.easing x values must be within [0, 1]";
      easing = p;
    } else if (isNumber(v)) {
      if (v.numberValue() < 0.0) return "motion." + key + " is negative";
      if (key == "duration") duration = v.numberValue();
      else values[key] = v.numberValue();
    } else if (key == "duration") {
      return "motion.duration is not a finite number";
    }  // other non-numeric keys are unknown and ignored
  }
  return {};
}

std::string parseFontSection(const core::JsonValue& font, std::string& family, std::vector<std::string>& fallback,
                             std::unordered_map<int, std::string>& files, std::optional<double>& body,
                             std::unordered_map<std::string, double>& sizes,
                             std::unordered_map<std::string, LineHeight>& lineHeights,
                             std::unordered_map<std::string, int>& weights,
                             std::unordered_map<std::string, double>& tracking) {
  if (const core::JsonValue* v = font.find("family")) {
    if (!v->isString() || v->stringValue().empty()) return "font.family is not a non-empty string";
    family = v->stringValue();
  }
  if (const core::JsonValue* v = font.find("fallback")) {
    if (v->type() != core::JsonType::Array) return "font.fallback is not an array";
    for (size_t i = 0; i < v->size(); ++i) {
      if (!v->child(i).isString()) return "font.fallback has a non-string entry";
      fallback.push_back(v->child(i).stringValue());
    }
  }
  if (const core::JsonValue* v = font.find("files")) {
    if (!v->isObject()) return "font.files is not an object";
    for (size_t i = 0; i < v->size(); ++i) {
      int weight = 0;
      const std::string& key = v->keyAt(i);
      const auto [end, ec] = std::from_chars(key.data(), key.data() + key.size(), weight);
      if (ec != std::errc() || end != key.data() + key.size() || weight < 1 || weight > 1000) {
        return "font.files." + key + " is not a weight between 1 and 1000";
      }
      if (!v->child(i).isString()) return "font.files." + key + " is not a string";
      files[weight] = v->child(i).stringValue();
    }
  }
  if (const core::JsonValue* v = font.find("bodySize")) {
    if (!isNumber(*v) || v->numberValue() <= 0.0) return "font.bodySize is not a positive number";
    body = v->numberValue();
  }
  if (const core::JsonValue* v = font.find("size")) {
    if (!v->isObject()) return "font.size is not an object";
    if (std::string e = numberMap(*v, "font.size", 0.0, true, sizes); !e.empty()) return e;
  }
  if (const core::JsonValue* v = font.find("lineHeight")) {
    if (!v->isObject()) return "font.lineHeight is not an object";
    std::unordered_map<std::string, double> raw;
    if (std::string e = numberMap(*v, "font.lineHeight", 0.0, true, raw); !e.empty()) return e;
    for (const auto& [name, value] : raw) lineHeights[name] = LineHeight{value, value <= kMaxLineHeightRatio};
  }
  if (const core::JsonValue* v = font.find("weight")) {
    if (!v->isObject()) return "font.weight is not an object";
    std::unordered_map<std::string, double> raw;
    if (std::string e = numberMap(*v, "font.weight", 0.0, true, raw); !e.empty()) return e;
    for (const auto& [name, value] : raw) {
      if (value > 1000.0 || value != std::floor(value)) return "font.weight." + name + " is not an integer in 1..1000";
      weights[name] = static_cast<int>(value);
    }
  }
  if (const core::JsonValue* v = font.find("tracking")) {
    if (!v->isObject()) return "font.tracking is not an object";
    if (std::string e = numberMap(*v, "font.tracking", -1.0, false, tracking); !e.empty()) return e;
  }
  return {};
}

}  // namespace

std::string Tokens::parseSections(const core::JsonValue& root, const ParseOptions& options) {
  struct Spec {
    Section section;
    const char* name;
  };
  static constexpr Spec kSpecs[] = {{Section::Space, "space"},   {Section::Radius, "radius"},
                                    {Section::Font, "font"},     {Section::Shadow, "shadow"},
                                    {Section::Motion, "motion"}, {Section::Widget, "widget"}};
  for (const Spec& spec : kSpecs) {
    const core::JsonValue* node = root.find(spec.name);
    if (node == nullptr) {
      if (options.requireAllSections) return std::string("missing section '") + spec.name + "'";
      continue;
    }
    if (!node->isObject()) return std::string("section '") + spec.name + "' is not an object";
    std::string error;
    switch (spec.section) {
      case Section::Space: error = numberMap(*node, "space", 0.0, false, space_); break;
      case Section::Radius: error = numberMap(*node, "radius", 0.0, false, radius_); break;
      case Section::Font:
        error = parseFontSection(*node, family_, fallback_, fontFiles_, bodySize_, fontSize_, lineHeight_,
                                 weight_, tracking_);
        break;
      case Section::Shadow: error = parseShadows(*node, shadows_); break;
      case Section::Motion: error = parseMotion(*node, motionDuration_, motionEasing_, motionValues_); break;
      case Section::Widget: error = flattenMetrics(*node, "", 0, metrics_, metricText_); break;
    }
    if (!error.empty()) return error;
    present_ |= 1u << static_cast<unsigned>(spec.section);
  }
  return {};
}

}  // namespace r1ui::theme
