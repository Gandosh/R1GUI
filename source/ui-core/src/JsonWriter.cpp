// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of the JSON string and number emitters declared in JsonWriter.h.
// Invariants: UTF-8 validation follows RFC 3629 (no overlongs, no surrogates, max U+10FFFF).
#include "r1ui/core/JsonWriter.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace r1ui::core {

namespace {

// Length of the valid UTF-8 sequence starting at text[i], or 0 if the bytes there are invalid.
size_t utf8SequenceLength(std::string_view text, size_t i) {
  const auto byteAt = [&](size_t k) { return static_cast<uint8_t>(text[k]); };
  const uint8_t lead = byteAt(i);
  size_t length = 0;
  uint32_t minimum = 0;
  uint32_t code = 0;
  if (lead < 0x80) return 1;
  if (lead >= 0xC2 && lead <= 0xDF) {
    length = 2;
    minimum = 0x80;
    code = lead & 0x1Fu;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    length = 3;
    minimum = 0x800;
    code = lead & 0x0Fu;
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    length = 4;
    minimum = 0x10000;
    code = lead & 0x07u;
  } else {
    return 0;
  }
  if (i + length > text.size()) return 0;
  for (size_t k = 1; k < length; ++k) {
    if ((byteAt(i + k) & 0xC0u) != 0x80u) return 0;
    code = (code << 6) | (byteAt(i + k) & 0x3Fu);
  }
  if (code < minimum || code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu)) return 0;
  return length;
}

}  // namespace

void appendQuoted(std::string& out, std::string_view text) {
  static constexpr char kHex[] = "0123456789abcdef";
  out.push_back('"');
  for (size_t i = 0; i < text.size();) {
    const auto c = static_cast<uint8_t>(text[i]);
    if (c == '"') {
      out += "\\\"";
      ++i;
    } else if (c == '\\') {
      out += "\\\\";
      ++i;
    } else if (c == '\n') {
      out += "\\n";
      ++i;
    } else if (c == '\r') {
      out += "\\r";
      ++i;
    } else if (c == '\t') {
      out += "\\t";
      ++i;
    } else if (c == '\b') {
      out += "\\b";
      ++i;
    } else if (c == '\f') {
      out += "\\f";
      ++i;
    } else if (c < 0x20) {
      out += "\\u00";
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0x0F]);
      ++i;
    } else {
      const size_t length = utf8SequenceLength(text, i);
      if (length == 0) {
        out += "\xEF\xBF\xBD";  // U+FFFD for each invalid byte
        ++i;
      } else {
        out.append(text.substr(i, length));
        i += length;
      }
    }
  }
  out.push_back('"');
}

void appendNumber(std::string& out, double value) {
  if (!std::isfinite(value)) throw std::invalid_argument("appendNumber: JSON cannot hold NaN or infinity");
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  if (result.ec != std::errc()) throw std::runtime_error("appendNumber: formatting failed");
  out.append(buffer, result.ptr);
}

}  // namespace r1ui::core
