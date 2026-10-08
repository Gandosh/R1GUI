// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: a small strict JSON (RFC 8259) value type and parser.
// Why: themes and layouts are loaded from files that may be damaged or hostile; one bounded,
//   exception-free parser keeps that boundary in one place.
// Callers: ui-theme (tokens.json) and later layout/icon loaders.
// Failure behavior: parseJson never throws on any input; it returns JsonResult::error with the
//   byte offset of the first problem. Limits (depth, input size, node count) are enforced before
//   memory grows. Objects keep member order; duplicate keys are rejected (not last-wins) so a
//   file cannot silently shadow an earlier value. Strings must be valid UTF-8; unicode escapes
//   must form valid surrogate pairs; numbers must fit a finite double.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace r1ui::core {

enum class JsonType { Null, Bool, Number, String, Array, Object };

// An immutable-after-parse JSON value. Arrays and objects store children in document order;
// for objects keyAt(i) names child(i).
class JsonValue {
 public:
  JsonValue() = default;
  static JsonValue makeBool(bool value);
  static JsonValue makeNumber(double value);
  static JsonValue makeString(std::string value);
  static JsonValue makeArray(std::vector<JsonValue> items);
  static JsonValue makeObject(std::vector<std::string> keys, std::vector<JsonValue> values);

  JsonType type() const { return type_; }
  bool isObject() const { return type_ == JsonType::Object; }
  bool isString() const { return type_ == JsonType::String; }

  // Scalar accessors return a neutral value (false, 0, empty) when the type does not match.
  bool boolValue() const { return type_ == JsonType::Bool && bool_; }
  double numberValue() const { return type_ == JsonType::Number ? number_ : 0.0; }
  const std::string& stringValue() const { return string_; }

  // Array/object children; empty for other types. child/keyAt throw std::out_of_range on a bad index.
  size_t size() const { return children_.size(); }
  const JsonValue& child(size_t index) const { return children_.at(index); }
  const std::string& keyAt(size_t index) const { return keys_.at(index); }

  // Object member lookup; nullptr when absent or when this is not an object.
  const JsonValue* find(std::string_view key) const;

 private:
  JsonType type_ = JsonType::Null;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  std::vector<std::string> keys_;  // objects only, parallel to children_
  std::vector<JsonValue> children_;
};

struct JsonLimits {
  size_t maxDepth = 64;
  size_t maxInputBytes = size_t{16} * 1024 * 1024;
  size_t maxNodes = 1'000'000;  // values of any kind, including containers
};

struct JsonError {
  size_t offset = 0;
  std::string message;
};

struct JsonResult {
  std::optional<JsonValue> value;
  JsonError error;  // meaningful only when value is empty
  bool ok() const { return value.has_value(); }
};

// Parses a complete JSON document; anything after the top-level value (except whitespace) is an
// error. Never throws.
JsonResult parseJson(std::string_view text, const JsonLimits& limits = {});

}  // namespace r1ui::core
