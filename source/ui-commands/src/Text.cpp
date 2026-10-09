// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Text.h.
// Invariants: sanitizeText always returns well-formed UTF-8 of at most maxBytes with no control
//   characters; all functions are pure.
// Callers: CommandRegistry, Chord parsing, OverrideIo.
#include "r1ui/commands/Text.h"

namespace r1ui::commands {

namespace {

constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

// Length of the well-formed UTF-8 sequence at the start of `s`, or 0 when it is malformed.
size_t sequenceLength(std::string_view s) {
  const auto byte = [&](size_t i) { return static_cast<unsigned char>(s[i]); };
  const auto cont = [&](size_t i) { return i < s.size() && (byte(i) & 0xC0) == 0x80; };
  const unsigned char b0 = byte(0);
  if (b0 < 0x80) return 1;
  if (b0 >= 0xC2 && b0 <= 0xDF) return cont(1) ? 2 : 0;
  if (b0 >= 0xE0 && b0 <= 0xEF) {
    if (!cont(1) || !cont(2)) return 0;
    if (b0 == 0xE0 && byte(1) < 0xA0) return 0;  // overlong
    if (b0 == 0xED && byte(1) >= 0xA0) return 0;  // surrogate
    return 3;
  }
  if (b0 >= 0xF0 && b0 <= 0xF4) {
    if (!cont(1) || !cont(2) || !cont(3)) return 0;
    if (b0 == 0xF0 && byte(1) < 0x90) return 0;  // overlong
    if (b0 == 0xF4 && byte(1) >= 0x90) return 0;  // above U+10FFFF
    return 4;
  }
  return 0;
}

bool identifierChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' || c == '/' || c == '-';
}

}  // namespace

bool isValidUtf8(std::string_view text) {
  size_t i = 0;
  while (i < text.size()) {
    const size_t length = sequenceLength(text.substr(i));
    if (length == 0) return false;
    i += length;
  }
  return true;
}

std::string sanitizeText(std::string_view text, size_t maxBytes) {
  std::string out;
  out.reserve(text.size() < maxBytes ? text.size() : maxBytes);
  size_t i = 0;
  while (i < text.size()) {
    size_t length = sequenceLength(text.substr(i));
    std::string_view piece = length != 0 ? text.substr(i, length) : kReplacement;
    if (length == 0) length = 1;
    const unsigned char first = static_cast<unsigned char>(text[i]);
    if (length == 1 && (first < 0x20 || first == 0x7F)) piece = " ";
    if (out.size() + piece.size() > maxBytes) break;
    out.append(piece);
    i += length;
  }
  return out;
}

bool isValidIdentifier(std::string_view text, size_t maxBytes) {
  if (text.empty() || text.size() > maxBytes) return false;
  for (const char c : text) {
    if (!identifierChar(c)) return false;
  }
  return true;
}

bool isValidIconName(std::string_view text) {
  if (text.empty() || text.size() > 64) return false;
  for (const char c : text) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

}  // namespace r1ui::commands
