// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: tokens.json file loading, structural validation and colour parsing for Tokens.h.
// Why: see Tokens.h. Validation completes on local data before the Tokens object is built, so a
//   rejected file leaves no partial state.
// Callers: Tokens.h consumers. Calls: r1ui::core::parseJson, <fstream>.
#include "r1ui/theme/Tokens.h"

#include <fstream>
#include <utility>

#include "r1ui/core/Json.h"

namespace r1ui::theme {

namespace {

constexpr size_t kMaxTokensFileBytes = size_t{4} * 1024 * 1024;
constexpr ThemeId kThemes[] = {ThemeId::Dark, ThemeId::Light};

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Parses "#rrggbb" or "#rrggbbaa"; std::nullopt for any other shape.
std::optional<Color> parseHexColor(const std::string& text) {
  if ((text.size() != 7 && text.size() != 9) || text[0] != '#') return std::nullopt;
  uint8_t channels[4] = {0, 0, 0, 255};
  for (size_t i = 0; i < (text.size() - 1) / 2; ++i) {
    const int hi = hexDigit(text[1 + 2 * i]);
    const int lo = hexDigit(text[2 + 2 * i]);
    if (hi < 0 || lo < 0) return std::nullopt;
    channels[i] = static_cast<uint8_t>(hi * 16 + lo);
  }
  return Color{channels[0], channels[1], channels[2], channels[3]};
}

TokensResult failure(std::string message) {
  TokensResult result;
  result.error = std::move(message);
  return result;
}

}  // namespace

const char* themeName(ThemeId theme) { return theme == ThemeId::Dark ? "dark" : "light"; }

std::string toHex(const Color& color) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out = "#";
  const uint8_t channels[4] = {color.r, color.g, color.b, color.a};
  const size_t count = color.a == 255 ? 3 : 4;
  for (size_t i = 0; i < count; ++i) {
    out.push_back(kDigits[channels[i] >> 4]);
    out.push_back(kDigits[channels[i] & 0x0F]);
  }
  return out;
}

TokensResult Tokens::loadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return failure("cannot open tokens file: " + path.string());
  file.seekg(0, std::ios::end);
  const std::streamoff size = file.tellg();
  if (size < 0) return failure("cannot determine size of tokens file: " + path.string());
  if (static_cast<unsigned long long>(size) > kMaxTokensFileBytes) {
    return failure("tokens file is too large: " + path.string());
  }
  file.seekg(0, std::ios::beg);
  std::string text(static_cast<size_t>(size), '\0');
  file.read(text.data(), size);
  if (file.gcount() != size) return failure("short read of tokens file: " + path.string());
  TokensResult result = parse(text);
  if (!result.ok()) result.error = path.string() + ": " + result.error;
  return result;
}

TokensResult Tokens::parse(std::string_view jsonText) {
  const core::JsonResult json = core::parseJson(jsonText);
  if (!json.ok()) {
    return failure("invalid JSON at byte " + std::to_string(json.error.offset) + ": " +
                   json.error.message);
  }
  const core::JsonValue* themes = json.value->find("themes");
  if (themes == nullptr || !themes->isObject()) return failure("missing object 'themes'");

  const core::JsonValue* sets[2] = {nullptr, nullptr};
  for (ThemeId id : kThemes) {
    const core::JsonValue* set = themes->find(themeName(id));
    if (set == nullptr || !set->isObject()) {
      return failure(std::string("missing object 'themes.") + themeName(id) + "'");
    }
    if (set->size() == 0) return failure(std::string("theme '") + themeName(id) + "' is empty");
    sets[static_cast<size_t>(id)] = set;
  }
  if (sets[0]->size() != sets[1]->size()) {
    return failure("dark and light themes have different numbers of colours");
  }

  Tokens tokens;
  for (ThemeId id : kThemes) {
    const core::JsonValue& set = *sets[static_cast<size_t>(id)];
    auto& colors = tokens.colors_[static_cast<size_t>(id)];
    for (size_t i = 0; i < set.size(); ++i) {
      const std::string& name = set.keyAt(i);
      const core::JsonValue& value = set.child(i);
      if (!value.isString()) {
        return failure(std::string("themes.") + themeName(id) + "." + name + " is not a string");
      }
      const std::optional<Color> color = parseHexColor(value.stringValue());
      if (!color) {
        return failure(std::string("themes.") + themeName(id) + "." + name +
                       " is not #rrggbb or #rrggbbaa");
      }
      if (id == ThemeId::Dark) {
        tokens.names_.push_back(name);
        tokens.index_.emplace(name, i);
        colors.push_back(*color);
      }
    }
  }

  // Light colours are stored in dark-theme order, found by name, so both sets must share names.
  const core::JsonValue& light = *sets[static_cast<size_t>(ThemeId::Light)];
  auto& lightColors = tokens.colors_[static_cast<size_t>(ThemeId::Light)];
  for (const std::string& name : tokens.names_) {
    const core::JsonValue* value = light.find(name);
    if (value == nullptr) return failure("light theme lacks colour '" + name + "'");
    lightColors.push_back(*parseHexColor(value->stringValue()));
  }

  TokensResult result;
  result.tokens = std::move(tokens);
  return result;
}

std::optional<Color> Tokens::colorAt(ThemeId theme, size_t index) const {
  const auto& colors = colors_[static_cast<size_t>(theme)];
  if (index >= colors.size()) return std::nullopt;
  return colors[index];
}

std::optional<Color> Tokens::color(ThemeId theme, std::string_view name) const {
  const auto it = index_.find(std::string(name));
  if (it == index_.end()) return std::nullopt;
  return colorAt(theme, it->second);
}

}  // namespace r1ui::theme
