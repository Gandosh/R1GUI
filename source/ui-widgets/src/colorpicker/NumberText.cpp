// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of NumberText.h.
// Invariants: parse validates the grammar by hand before calling from_chars, so from_chars is never
//   asked to accept "inf", "nan", hex floats or a sign-only string; results are finite.
// Callers: the picker and editor entry fields.
#include "r1ui/widgets/colorpicker/NumberText.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace r1ui::widgets {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

std::optional<double> parseNumber(std::string_view text) {
  while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
  while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
  if (text.empty() || text.size() > 64) return std::nullopt;
  size_t i = 0;
  if (text[i] == '+' || text[i] == '-') ++i;
  size_t mantissaDigits = 0;
  while (i < text.size() && isDigit(text[i])) { ++i; ++mantissaDigits; }
  if (i < text.size() && text[i] == '.') {
    ++i;
    while (i < text.size() && isDigit(text[i])) { ++i; ++mantissaDigits; }
  }
  if (mantissaDigits == 0) return std::nullopt;
  if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
    ++i;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
    size_t exponentDigits = 0;
    while (i < text.size() && isDigit(text[i])) { ++i; ++exponentDigits; }
    if (exponentDigits == 0) return std::nullopt;
  }
  if (i != text.size()) return std::nullopt;
  // from_chars rejects a leading '+' and ".5" style forms are fine; build a clean copy.
  std::string clean;
  clean.reserve(text.size() + 1);
  size_t start = 0;
  if (text[0] == '-') { clean.push_back('-'); start = 1; }
  else if (text[0] == '+') start = 1;
  if (start < text.size() && text[start] == '.') clean.push_back('0');
  clean.append(text.substr(start));
  double value = 0.0;
  const auto result = std::from_chars(clean.data(), clean.data() + clean.size(), value);
  if (result.ec != std::errc() || result.ptr != clean.data() + clean.size() || !std::isfinite(value)) return std::nullopt;
  return value;
}

std::string formatNumber(double value, int maxFractionDigits) {
  if (!std::isfinite(value)) return "0";
  const int digits = std::clamp(maxFractionDigits, 0, 9);
  char buffer[400];
  const int n = std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(buffer)) return "0";
  std::string s(buffer, static_cast<size_t>(n));
  if (s.find('.') != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  if (s == "-0" || s.empty()) return "0";
  return s;
}

}  // namespace r1ui::widgets
