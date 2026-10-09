// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the validated, immutable token set: colours for the dark and light themes plus the
//   theme-independent sections space, radius, font, shadow, motion and widget metrics.
// Why: widgets and the preview read tokens by name; this is the single place that turns
//   tokens.json into checked values so a damaged file cannot reach rendering code.
// Sections: `themes` is mandatory. The other sections are optional unless
//   ParseOptions::requireAllSections is set; an absent section simply makes its accessors return
//   std::nullopt. A section that is present must be fully valid (types, finite numbers,
//   non-negative sizes, shadow blur >= 0, weights 1..1000) or the whole file is rejected.
//   Unknown keys are ignored, and keys starting with '_' are metadata. Units are logical pixels
//   at 100% scale unless stated.
// Callers: examples/preview now; ui-widgets later. Calls: r1ui::core::parseJson.
// Failure behavior: loading returns an error string and no Tokens object (no partial state);
//   nothing throws on bad file content. Colour names keep the order of the dark theme in the file.
// Colour space: sRGB, straight (non-premultiplied) alpha, 8 bits per channel.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace r1ui::core {
class JsonValue;
}

namespace r1ui::theme {

struct Color {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
  friend bool operator==(const Color&, const Color&) = default;
};

enum class ThemeId { Dark = 0, Light = 1 };

// Lower-case theme name as used in the file and in UI text ("dark", "light").
const char* themeName(ThemeId theme);

// Formats "#rrggbb", or "#rrggbbaa" when alpha is not 255.
std::string toHex(const Color& color);

// One layer of a box shadow: offsets, blur radius (>= 0), spread (may be negative) and colour.
struct ShadowLayer {
  double offsetX = 0.0;
  double offsetY = 0.0;
  double blur = 0.0;
  double spread = 0.0;
  Color color;
  friend bool operator==(const ShadowLayer&, const ShadowLayer&) = default;
};

// A line height token: absolute pixels, or a multiplier of the font size. Values up to
// kMaxLineHeightRatio are ratios (tokens such as tight = 1.25); larger values are pixels.
inline constexpr double kMaxLineHeightRatio = 3.0;
struct LineHeight {
  double value = 0.0;
  bool relative = false;
  double resolve(double fontSizePx) const { return relative ? value * fontSizePx : value; }
};

enum class Section { Space, Radius, Font, Shadow, Motion, Widget };

struct ParseOptions {
  bool requireAllSections = false;  // reject files lacking any of the Section values
};

struct TokensResult;

// Parses a colour "#rrggbb" or "#rrggbbaa"; std::nullopt for any other text.
std::optional<Color> parseColor(std::string_view text);

class Tokens {
 public:
  // Reads and validates a tokens.json file (size-limited). Never throws on bad content.
  static TokensResult loadFile(const std::filesystem::path& path, const ParseOptions& options = {});
  // Validates tokens.json text: themes.dark and themes.light must be objects with the same
  // non-empty set of names, and every value must be "#rrggbb" or "#rrggbbaa".
  static TokensResult parse(std::string_view jsonText, const ParseOptions& options = {});

  // Colour names in file order (the dark theme's order).
  const std::vector<std::string>& colorNames() const { return names_; }
  size_t colorCount() const { return names_.size(); }

  // Colour by index into colorNames(); std::nullopt when out of range.
  std::optional<Color> colorAt(ThemeId theme, size_t index) const;
  // Colour by name; std::nullopt when the name is unknown.
  std::optional<Color> color(ThemeId theme, std::string_view name) const;

  bool hasSection(Section section) const { return (present_ & (1u << static_cast<unsigned>(section))) != 0; }

  // ---- theme-independent tokens; std::nullopt when the section or name is absent ----
  std::optional<double> space(std::string_view name) const { return lookup(space_, name); }
  std::optional<double> radius(std::string_view name) const { return lookup(radius_, name); }
  std::optional<double> fontSize(std::string_view name) const { return lookup(fontSize_, name); }
  std::optional<LineHeight> lineHeight(std::string_view name) const;
  std::optional<int> fontWeight(std::string_view name) const;
  std::optional<double> letterSpacing(std::string_view name) const { return lookup(tracking_, name); }
  std::optional<double> bodyFontSize() const { return bodySize_; }
  const std::string& fontFamily() const { return family_; }
  const std::vector<std::string>& fontFallback() const { return fallback_; }
  // Font file name for a numeric weight (400, 500, ...).
  std::optional<std::string> fontFile(int weight) const;
  // Shadow layers, outermost list entry first as written in the file.
  std::optional<std::vector<ShadowLayer>> shadow(std::string_view name) const;
  std::optional<double> motionDuration() const { return motionDuration_; }  // seconds
  // Cubic-bezier control points x1, y1, x2, y2.
  std::optional<std::array<double, 4>> motionEasing() const { return motionEasing_; }
  std::optional<double> motionValue(std::string_view name) const { return lookup(motionValues_, name); }
  // Numeric widget metric, e.g. widgetMetric("field", "height") or ("button", "sm.height").
  std::optional<double> widgetMetric(std::string_view widget, std::string_view key) const;
  // Text-valued widget metric (a reference such as a font size name).
  std::optional<std::string> widgetMetricText(std::string_view widget, std::string_view key) const;

 private:
  Tokens() = default;
  template <class V>
  static std::optional<V> lookup(const std::unordered_map<std::string, V>& map, std::string_view name) {
    const auto it = map.find(std::string(name));
    if (it == map.end()) return std::nullopt;
    return it->second;
  }
  // Validates and stores the optional sections (TokenSections.cpp); returns an error or "".
  std::string parseSections(const core::JsonValue& root, const ParseOptions& options);

  uint32_t present_ = 0;
  std::unordered_map<std::string, double> space_, radius_, fontSize_, tracking_, motionValues_;
  std::unordered_map<std::string, LineHeight> lineHeight_;
  std::unordered_map<std::string, int> weight_;
  std::unordered_map<int, std::string> fontFiles_;
  std::unordered_map<std::string, std::vector<ShadowLayer>> shadows_;
  std::unordered_map<std::string, double> metrics_;
  std::unordered_map<std::string, std::string> metricText_;
  std::string family_;
  std::vector<std::string> fallback_;
  std::optional<double> bodySize_;
  std::optional<double> motionDuration_;
  std::optional<std::array<double, 4>> motionEasing_;
  std::vector<std::string> names_;
  std::vector<Color> colors_[2];  // indexed by ThemeId, parallel to names_
  std::unordered_map<std::string, size_t> index_;
};

// Outcome of loading: either tokens or a human-readable error.
struct TokensResult {
  std::optional<Tokens> tokens;
  std::string error;  // empty on success
  bool ok() const { return tokens.has_value(); }
};

}  // namespace r1ui::theme
