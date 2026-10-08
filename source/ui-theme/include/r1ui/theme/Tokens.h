// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the validated, immutable colour-token set for the dark and light themes.
// Why: widgets and the preview read colours by theme and name; this is the single place that
//   turns tokens.json into checked values so a damaged file cannot reach rendering code.
// Callers: examples/preview now; ui-widgets later. Calls: r1ui::core::parseJson.
// Failure behavior: loading returns an error string and no Tokens object (no partial state);
//   nothing throws on bad file content. Colour names keep the order of the dark theme in the file.
// Colour space: sRGB, straight (non-premultiplied) alpha, 8 bits per channel.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

struct TokensResult;

class Tokens {
 public:
  // Reads and validates a tokens.json file (size-limited). Never throws on bad content.
  static TokensResult loadFile(const std::filesystem::path& path);
  // Validates tokens.json text: themes.dark and themes.light must be objects with the same
  // non-empty set of names, and every value must be "#rrggbb" or "#rrggbbaa".
  static TokensResult parse(std::string_view jsonText);

  // Colour names in file order (the dark theme's order).
  const std::vector<std::string>& colorNames() const { return names_; }
  size_t colorCount() const { return names_.size(); }

  // Colour by index into colorNames(); std::nullopt when out of range.
  std::optional<Color> colorAt(ThemeId theme, size_t index) const;
  // Colour by name; std::nullopt when the name is unknown.
  std::optional<Color> color(ThemeId theme, std::string_view name) const;

 private:
  Tokens() = default;
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
