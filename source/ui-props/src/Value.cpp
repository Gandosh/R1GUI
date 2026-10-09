// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the implementation of Value.h (equality, UTF-8 validation, text form and parsing of values).
// Why: see Value.h. Parsing is the boundary for pasted text and typed values, so it is strict and total:
//   it never throws, never reads past the text, rejects non-finite numbers and bounds the length it accepts.
// Callers: Descriptor.cpp, PropertyContext*.cpp, tests.
#include "r1ui/props/Value.h"

#include <array>
#include <charconv>
#include <cmath>
#include <system_error>
#include <vector>

namespace r1ui::props {

namespace {

constexpr size_t kMaxNumberText = 64;  // a double never needs more; longer tokens are rejected outright

std::string_view trim(std::string_view s) {
  const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!s.empty() && blank(s.front())) s.remove_prefix(1);
  while (!s.empty() && blank(s.back())) s.remove_suffix(1);
  return s;
}

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}

// A finite double from a whole token (optional sign, no hex, no "nan"/"inf" words).
std::optional<double> parseDouble(std::string_view token) {
  token = trim(token);
  if (token.empty() || token.size() > kMaxNumberText) return std::nullopt;
  if (token.front() == '+') token.remove_prefix(1);
  if (token.empty() || token.front() == '+') return std::nullopt;
  for (const char c : token) {
    const bool ok = (c >= '0' && c <= '9') || c == '.' || c == '-' || c == 'e' || c == 'E' || c == '+';
    if (!ok) return std::nullopt;
  }
  double out = 0.0;
  const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), out);
  if (ec != std::errc() || end != token.data() + token.size() || !std::isfinite(out)) return std::nullopt;
  return out;
}

std::optional<int64_t> parseInt(std::string_view token) {
  token = trim(token);
  if (token.empty() || token.size() > kMaxNumberText) return std::nullopt;
  if (token.front() == '+') token.remove_prefix(1);
  int64_t out = 0;
  const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), out);
  if (ec != std::errc() || end != token.data() + token.size()) return std::nullopt;
  return out;
}

std::string numberText(double v) {
  std::array<char, 40> buffer{};
  const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v);
  if (ec != std::errc()) return {};
  return std::string(buffer.data(), end);
}

// Splits "a, b c" into numeric tokens separated by commas and/or blanks; empty on a bad token.
std::vector<double> parseNumberList(std::string_view text, size_t expected) {
  std::vector<double> out;
  size_t pos = 0;
  while (pos < text.size()) {
    while (pos < text.size() && (text[pos] == ',' || text[pos] == ' ' || text[pos] == '\t')) ++pos;
    if (pos >= text.size()) break;
    size_t end = pos;
    while (end < text.size() && text[end] != ',' && text[end] != ' ' && text[end] != '\t') ++end;
    const auto number = parseDouble(text.substr(pos, end - pos));
    if (!number || out.size() >= expected) return {};
    out.push_back(*number);
    pos = end;
  }
  if (out.size() != expected) return {};
  return out;
}

std::optional<int> hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return std::nullopt;
}

std::optional<Color> parseHexColor(std::string_view text) {
  text.remove_prefix(1);  // the '#'
  if (text.size() != 3 && text.size() != 4 && text.size() != 6 && text.size() != 8) return std::nullopt;
  std::array<int, 8> digits{};
  for (size_t i = 0; i < text.size(); ++i) {
    const auto d = hexDigit(text[i]);
    if (!d) return std::nullopt;
    digits[i] = *d;
  }
  const bool shortForm = text.size() <= 4;
  const auto channel = [&](size_t index) {
    const int v = shortForm ? digits[index] * 17 : digits[index * 2] * 16 + digits[index * 2 + 1];
    return static_cast<float>(v) / 255.0f;
  };
  Color c;
  c.r = channel(0);
  c.g = channel(1);
  c.b = channel(2);
  c.a = (text.size() == 4 || text.size() == 8) ? channel(3) : 1.0f;
  return c;
}

}  // namespace

const char* kindName(ValueKind kind) {
  switch (kind) {
    case ValueKind::Bool: return "bool";
    case ValueKind::Int: return "int";
    case ValueKind::Double: return "double";
    case ValueKind::String: return "string";
    case ValueKind::Enum: return "enum";
    case ValueKind::Color: return "color";
    case ValueKind::Vec2: return "vec2";
    case ValueKind::Vec3: return "vec3";
    case ValueKind::Range: return "range";
  }
  return "?";
}

// ---- queries ------------------------------------------------------------------------------------------

bool matchesKind(const Value& v, ValueKind kind) { return storageOf(v) == storageOf(kind); }

Value zeroValue(ValueKind kind) {
  switch (storageOf(kind)) {
    case Storage::Bool: return false;
    case Storage::Int: return int64_t{0};
    case Storage::Double: return 0.0;
    case Storage::String: return std::string();
    case Storage::Color: return Color{0.0f, 0.0f, 0.0f, 1.0f};
    case Storage::Vec2: return Vec2{};
    case Storage::Vec3: return Vec3{};
  }
  return false;
}

namespace {
bool sameNumber(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }
bool sameNumber(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }
}  // namespace

bool valuesEqual(const Value& a, const Value& b) {
  if (a.index() != b.index()) return false;
  switch (storageOf(a)) {
    case Storage::Double: return sameNumber(std::get<double>(a), std::get<double>(b));
    case Storage::Color: {
      const Color& x = std::get<Color>(a);
      const Color& y = std::get<Color>(b);
      return sameNumber(x.r, y.r) && sameNumber(x.g, y.g) && sameNumber(x.b, y.b) && sameNumber(x.a, y.a);
    }
    case Storage::Vec2: {
      const Vec2& x = std::get<Vec2>(a);
      const Vec2& y = std::get<Vec2>(b);
      return sameNumber(x.x, y.x) && sameNumber(x.y, y.y);
    }
    case Storage::Vec3: {
      const Vec3& x = std::get<Vec3>(a);
      const Vec3& y = std::get<Vec3>(b);
      return sameNumber(x.x, y.x) && sameNumber(x.y, y.y) && sameNumber(x.z, y.z);
    }
    default: return a == b;
  }
}

bool isFinite(const Value& v) {
  switch (storageOf(v)) {
    case Storage::Double: return std::isfinite(std::get<double>(v));
    case Storage::Color: {
      const Color& c = std::get<Color>(v);
      return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a);
    }
    case Storage::Vec2: {
      const Vec2& p = std::get<Vec2>(v);
      return std::isfinite(p.x) && std::isfinite(p.y);
    }
    case Storage::Vec3: {
      const Vec3& p = std::get<Vec3>(v);
      return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    default: return true;
  }
}

std::optional<double> componentOf(const Value& v, size_t index) {
  switch (storageOf(v)) {
    case Storage::Int: return static_cast<double>(std::get<int64_t>(v));
    case Storage::Double: return std::get<double>(v);
    case Storage::Vec2: {
      const Vec2& p = std::get<Vec2>(v);
      return index == 0 ? p.x : index == 1 ? p.y : std::optional<double>();
    }
    case Storage::Vec3: {
      const Vec3& p = std::get<Vec3>(v);
      return index == 0 ? p.x : index == 1 ? p.y : index == 2 ? p.z : std::optional<double>();
    }
    case Storage::Color: {
      const Color& c = std::get<Color>(v);
      return index == 0 ? c.r : index == 1 ? c.g : index == 2 ? c.b : index == 3 ? c.a : std::optional<double>();
    }
    default: return std::nullopt;
  }
}

Value withComponent(const Value& v, size_t index, double component) {
  Value out = v;
  if (auto* p = std::get_if<Vec2>(&out)) {
    if (index == 0) p->x = component;
    if (index == 1) p->y = component;
  } else if (auto* q = std::get_if<Vec3>(&out)) {
    if (index == 0) q->x = component;
    if (index == 1) q->y = component;
    if (index == 2) q->z = component;
  } else if (auto* c = std::get_if<Color>(&out)) {
    // A colour channel is a float: a value beyond float range becomes infinite and is refused by validate().
    const float channel = static_cast<float>(component);
    if (index == 0) c->r = channel;
    if (index == 1) c->g = channel;
    if (index == 2) c->b = channel;
    if (index == 3) c->a = channel;
  }
  return out;
}

size_t valueBytes(const Value& v) {
  size_t bytes = sizeof(Value);
  if (const auto* s = std::get_if<std::string>(&v)) bytes += s->capacity();
  return bytes;
}

// ---- text ---------------------------------------------------------------------------------------------

bool isValidUtf8(std::string_view text) {
  size_t i = 0;
  while (i < text.size()) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    size_t length = 0;
    uint32_t cp = 0;
    if (c < 0x80) {
      ++i;
      continue;
    }
    if (c >= 0xC2 && c <= 0xDF) {
      length = 2;
      cp = c & 0x1Fu;
    } else if (c >= 0xE0 && c <= 0xEF) {
      length = 3;
      cp = c & 0x0Fu;
    } else if (c >= 0xF0 && c <= 0xF4) {
      length = 4;
      cp = c & 0x07u;
    } else {
      return false;
    }
    if (i + length > text.size()) return false;
    for (size_t k = 1; k < length; ++k) {
      const unsigned char cont = static_cast<unsigned char>(text[i + k]);
      if ((cont & 0xC0u) != 0x80u) return false;
      cp = (cp << 6) | (cont & 0x3Fu);
    }
    if (length == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return false;
    if (length == 4 && (cp < 0x10000 || cp > 0x10FFFF)) return false;
    i += length;
  }
  return true;
}

std::string formatValue(const Value& v) {
  if (!isFinite(v)) return {};
  switch (storageOf(v)) {
    case Storage::Bool: return std::get<bool>(v) ? "true" : "false";
    case Storage::Int: return std::to_string(std::get<int64_t>(v));
    case Storage::Double: return numberText(std::get<double>(v));
    case Storage::String: return std::get<std::string>(v);
    case Storage::Color: {
      const Color& c = std::get<Color>(v);
      return "rgba(" + numberText(c.r) + "," + numberText(c.g) + "," + numberText(c.b) + "," + numberText(c.a) + ")";
    }
    case Storage::Vec2: {
      const Vec2& p = std::get<Vec2>(v);
      return "(" + numberText(p.x) + "," + numberText(p.y) + ")";
    }
    case Storage::Vec3: {
      const Vec3& p = std::get<Vec3>(v);
      return "(" + numberText(p.x) + "," + numberText(p.y) + "," + numberText(p.z) + ")";
    }
  }
  return {};
}

std::optional<Value> parseValue(std::string_view text, ValueKind kind) {
  switch (kind) {
    case ValueKind::Bool: {
      text = trim(text);
      for (const std::string_view yes : {"true", "on", "yes", "1"}) {
        if (equalsIgnoreCase(text, yes)) return Value(true);
      }
      for (const std::string_view no : {"false", "off", "no", "0"}) {
        if (equalsIgnoreCase(text, no)) return Value(false);
      }
      return std::nullopt;
    }
    case ValueKind::Int:
    case ValueKind::Enum: {
      if (const auto i = parseInt(text)) return Value(*i);
      return std::nullopt;
    }
    case ValueKind::Double:
    case ValueKind::Range: {
      if (const auto d = parseDouble(text)) return Value(*d);
      return std::nullopt;
    }
    case ValueKind::String: return Value(std::string(text));
    case ValueKind::Color: {
      text = trim(text);
      if (text.empty()) return std::nullopt;
      if (text.front() == '#') {
        if (const auto c = parseHexColor(text)) return Value(*c);
        return std::nullopt;
      }
      constexpr std::string_view prefix = "rgba(";
      if (text.size() > prefix.size() + 1 && equalsIgnoreCase(text.substr(0, prefix.size()), prefix) && text.back() == ')') {
        const auto list = parseNumberList(text.substr(prefix.size(), text.size() - prefix.size() - 1), 4);
        if (list.size() == 4) {
          return Value(Color{static_cast<float>(list[0]), static_cast<float>(list[1]), static_cast<float>(list[2]), static_cast<float>(list[3])});
        }
      }
      return std::nullopt;
    }
    case ValueKind::Vec2:
    case ValueKind::Vec3: {
      text = trim(text);
      if (text.size() >= 2 && text.front() == '(' && text.back() == ')') text = text.substr(1, text.size() - 2);
      const size_t n = componentCount(kind);
      const auto list = parseNumberList(text, n);
      if (list.size() != n) return std::nullopt;
      if (n == 2) return Value(Vec2{list[0], list[1]});
      return Value(Vec3{list[0], list[1], list[2]});
    }
  }
  return std::nullopt;
}

}  // namespace r1ui::props
