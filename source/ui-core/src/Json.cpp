// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the recursive-descent implementation of parseJson and the JsonValue factories.
// Why: see Json.h. Recursion depth is bounded by JsonLimits::maxDepth, so stack use is fixed.
// Callers: Json.h consumers. Calls: <charconv> for double conversion (locale independent).
// Internal failure uses a private exception caught inside parseJson; none escapes.
#include "r1ui/core/Json.h"

#include <charconv>
#include <new>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace r1ui::core {

// ---- JsonValue ------------------------------------------------------------------------

JsonValue JsonValue::makeBool(bool value) {
  JsonValue v;
  v.type_ = JsonType::Bool;
  v.bool_ = value;
  return v;
}

JsonValue JsonValue::makeNumber(double value) {
  JsonValue v;
  v.type_ = JsonType::Number;
  v.number_ = value;
  return v;
}

JsonValue JsonValue::makeString(std::string value) {
  JsonValue v;
  v.type_ = JsonType::String;
  v.string_ = std::move(value);
  return v;
}

JsonValue JsonValue::makeArray(std::vector<JsonValue> items) {
  JsonValue v;
  v.type_ = JsonType::Array;
  v.children_ = std::move(items);
  return v;
}

JsonValue JsonValue::makeObject(std::vector<std::string> keys, std::vector<JsonValue> values) {
  JsonValue v;
  v.type_ = JsonType::Object;
  v.keys_ = std::move(keys);
  v.children_ = std::move(values);
  return v;
}

const JsonValue* JsonValue::find(std::string_view key) const {
  if (type_ != JsonType::Object) return nullptr;
  for (size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) return &children_[i];
  }
  return nullptr;
}

// ---- Parser ---------------------------------------------------------------------------

namespace {

struct ParseFailure {
  size_t offset;
  const char* message;
};

class Parser {
 public:
  Parser(std::string_view text, const JsonLimits& limits) : text_(text), limits_(limits) {}

  JsonValue parseDocument() {
    skipSpace();
    JsonValue value = parseValue(0);
    skipSpace();
    if (pos_ != text_.size()) fail("unexpected data after the top-level value");
    return value;
  }

 private:
  [[noreturn]] void fail(const char* message) const { throw ParseFailure{pos_, message}; }

  bool atEnd() const { return pos_ >= text_.size(); }
  char peek() const { return text_[pos_]; }

  void skipSpace() {
    while (!atEnd()) {
      const char c = peek();
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
      ++pos_;
    }
  }

  void expect(char c, const char* message) {
    if (atEnd() || peek() != c) fail(message);
    ++pos_;
  }

  void countNode() {
    if (++nodes_ > limits_.maxNodes) fail("too many values");
  }

  JsonValue parseValue(size_t depth) {
    if (atEnd()) fail("unexpected end of input");
    countNode();
    switch (peek()) {
      case '{': return parseObject(depth);
      case '[': return parseArray(depth);
      case '"': return JsonValue::makeString(parseString());
      case 't': parseLiteral("true"); return JsonValue::makeBool(true);
      case 'f': parseLiteral("false"); return JsonValue::makeBool(false);
      case 'n': parseLiteral("null"); return JsonValue();
      default: return parseNumber();
    }
  }

  void parseLiteral(std::string_view word) {
    if (text_.substr(pos_, word.size()) != word) fail("invalid literal");
    pos_ += word.size();
  }

  JsonValue parseObject(size_t depth) {
    if (depth >= limits_.maxDepth) fail("nesting too deep");
    ++pos_;  // '{'
    std::vector<std::string> keys;
    std::vector<JsonValue> values;
    std::unordered_set<std::string> seen;
    skipSpace();
    if (!atEnd() && peek() == '}') {
      ++pos_;
      return JsonValue::makeObject({}, {});
    }
    for (;;) {
      skipSpace();
      if (atEnd() || peek() != '"') fail("expected a string key");
      const size_t keyOffset = pos_;
      std::string key = parseString();
      if (!seen.insert(key).second) {
        pos_ = keyOffset;
        fail("duplicate object key");
      }
      skipSpace();
      expect(':', "expected ':' after key");
      skipSpace();
      values.push_back(parseValue(depth + 1));
      keys.push_back(std::move(key));
      skipSpace();
      if (atEnd()) fail("unterminated object");
      if (peek() == ',') {
        ++pos_;
        continue;
      }
      expect('}', "expected ',' or '}'");
      return JsonValue::makeObject(std::move(keys), std::move(values));
    }
  }

  JsonValue parseArray(size_t depth) {
    if (depth >= limits_.maxDepth) fail("nesting too deep");
    ++pos_;  // '['
    std::vector<JsonValue> items;
    skipSpace();
    if (!atEnd() && peek() == ']') {
      ++pos_;
      return JsonValue::makeArray({});
    }
    for (;;) {
      skipSpace();
      items.push_back(parseValue(depth + 1));
      skipSpace();
      if (atEnd()) fail("unterminated array");
      if (peek() == ',') {
        ++pos_;
        continue;
      }
      expect(']', "expected ',' or ']'");
      return JsonValue::makeArray(std::move(items));
    }
  }

  static bool isDigit(char c) { return c >= '0' && c <= '9'; }

  // Number grammar per RFC 8259: -? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE] [+-]? [0-9]+)?
  JsonValue parseNumber() {
    const size_t start = pos_;
    if (!atEnd() && peek() == '-') ++pos_;
    if (atEnd() || !isDigit(peek())) fail("invalid number");
    if (peek() == '0') {
      ++pos_;
    } else {
      while (!atEnd() && isDigit(peek())) ++pos_;
    }
    if (!atEnd() && peek() == '.') {
      ++pos_;
      if (atEnd() || !isDigit(peek())) fail("digit expected after '.'");
      while (!atEnd() && isDigit(peek())) ++pos_;
    }
    if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
      ++pos_;
      if (!atEnd() && (peek() == '+' || peek() == '-')) ++pos_;
      if (atEnd() || !isDigit(peek())) fail("digit expected in exponent");
      while (!atEnd() && isDigit(peek())) ++pos_;
    }
    double value = 0.0;
    const char* first = text_.data() + start;
    const char* last = text_.data() + pos_;
    const auto [end, ec] = std::from_chars(first, last, value);
    if (ec == std::errc::result_out_of_range) {
      pos_ = start;
      fail("number out of range");
    }
    if (ec != std::errc() || end != last) {
      pos_ = start;
      fail("invalid number");
    }
    return JsonValue::makeNumber(value);
  }

  unsigned parseHex4() {
    if (text_.size() - pos_ < 4) fail("truncated unicode escape");
    unsigned value = 0;
    for (size_t i = 0; i < 4; ++i) {
      const char c = text_[pos_ + i];
      unsigned digit = 0;
      if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a') + 10;
      else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A') + 10;
      else fail("invalid hex digit in unicode escape");
      value = value * 16 + digit;
    }
    pos_ += 4;
    return value;
  }

  static void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  // Copies one multi-byte UTF-8 sequence starting at pos_, rejecting overlong forms,
  // surrogates, code points above U+10FFFF and truncated or malformed continuation bytes.
  void copyUtf8Sequence(std::string& out) {
    const auto lead = static_cast<unsigned char>(peek());
    size_t length = 0;
    unsigned cp = 0;
    unsigned minimum = 0;
    if (lead >= 0xC2 && lead <= 0xDF) {
      length = 2;
      cp = lead & 0x1Fu;
      minimum = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
      length = 3;
      cp = lead & 0x0Fu;
      minimum = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
      length = 4;
      cp = lead & 0x07u;
      minimum = 0x10000;
    } else {
      fail("invalid UTF-8 lead byte");
    }
    if (text_.size() - pos_ < length) fail("truncated UTF-8 sequence");
    for (size_t i = 1; i < length; ++i) {
      const auto byte = static_cast<unsigned char>(text_[pos_ + i]);
      if ((byte & 0xC0) != 0x80) fail("invalid UTF-8 continuation byte");
      cp = (cp << 6) | (byte & 0x3Fu);
    }
    if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
      fail("invalid UTF-8 code point");
    }
    out.append(text_.data() + pos_, length);
    pos_ += length;
  }

  // Parses a unicode escape body (after the 'u'), combining a surrogate pair into one code point.
  unsigned parseUnicodeEscape() {
    unsigned cp = parseHex4();
    if (cp >= 0xDC00 && cp <= 0xDFFF) fail("lone low surrogate");
    if (cp >= 0xD800 && cp <= 0xDBFF) {
      if (text_.substr(pos_, 2) != "\\u") fail("lone high surrogate");
      pos_ += 2;
      const unsigned low = parseHex4();
      if (low < 0xDC00 || low > 0xDFFF) fail("high surrogate not followed by low surrogate");
      cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
    }
    return cp;
  }

  std::string parseString() {
    ++pos_;  // opening quote
    std::string out;
    for (;;) {
      if (atEnd()) fail("unterminated string");
      const auto c = static_cast<unsigned char>(peek());
      if (c == '"') {
        ++pos_;
        return out;
      }
      if (c < 0x20) fail("control character in string");
      if (c >= 0x80) {
        copyUtf8Sequence(out);
        continue;
      }
      if (c != '\\') {
        out.push_back(static_cast<char>(c));
        ++pos_;
        continue;
      }
      ++pos_;
      if (atEnd()) fail("unterminated escape");
      const char e = peek();
      ++pos_;
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': appendUtf8(out, parseUnicodeEscape()); break;
        default:
          --pos_;
          fail("invalid escape");
      }
    }
  }

  std::string_view text_;
  const JsonLimits& limits_;
  size_t pos_ = 0;
  size_t nodes_ = 0;
};

}  // namespace

JsonResult parseJson(std::string_view text, const JsonLimits& limits) {
  JsonResult result;
  if (text.size() > limits.maxInputBytes) {
    result.error = {0, "input exceeds the size limit"};
    return result;
  }
  try {
    result.value = Parser(text, limits).parseDocument();
  } catch (const ParseFailure& failure) {
    result.error = {failure.offset, failure.message};
  } catch (const std::bad_alloc&) {
    result.error = {0, "out of memory"};
  }
  return result;
}

}  // namespace r1ui::core
