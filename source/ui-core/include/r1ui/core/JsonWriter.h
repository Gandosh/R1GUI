// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the two primitives needed to emit JSON text that core::parseJson reads back exactly:
//   string quoting with correct escapes, and shortest round-trip number formatting.
// Why: layouts are written by several modules; hand-built strings are a classic escaping and
//   locale bug source, so the escaping rules live in one place.
// Callers: ui-dock (layout serialisation). Output is always valid UTF-8 JSON (RFC 8259).
// Failure behavior: appendNumber throws std::invalid_argument for NaN or infinity (JSON cannot
//   represent them); appendQuoted never fails (invalid UTF-8 bytes are replaced by U+FFFD).
#pragma once

#include <string>
#include <string_view>

namespace r1ui::core {

// Appends `text` as a quoted JSON string: quote, backslash and control characters are escaped
// (short escapes where JSON has them, \u00XX otherwise); other valid UTF-8 is written as is.
void appendQuoted(std::string& out, std::string_view text);

// Appends the shortest decimal form that parses back to exactly `value` (locale independent).
void appendNumber(std::string& out, double value);

}  // namespace r1ui::core
