// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the arithmetic expression language of typed property values: + - * /, parentheses, unary
//   signs, number literals with an optional unit suffix converted to the stored unit, and the word
//   "Mixed" (any case) standing for the value each selected object currently has.
// Why: spec 09 rules 18, 19 and 41: a typed value may carry units, and an expression over the mixed
//   placeholder is evaluated per object so every object keeps its offset ("Mixed + 5").
// Callers: PropertyContext (typed edits, expression edits), tests. The widget layer uses its own parser
//   for the NumberField text; both accept the same grammar.
// Boundary rules (the text comes from a user or the clipboard): at most kMaxExpressionBytes bytes,
//   kMaxExpressionNodes nodes and kMaxExpressionDepth nesting levels; only ASCII digits, '.', exponents
//   and the operators (plus U+2212 as a minus) are understood; "nan", "inf" and hex are rejected; a
//   result that is not finite is NaN. Every failure returns nullopt or NaN and never throws.
// Grammar: expr = term {("+"|"-") term}; term = unary {("*"|"/") unary}; unary = {"+"|"-"} primary;
//   primary = number [unit] | "Mixed" | "(" expr ")". A unit applies to the number it follows.
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace r1ui::props {

inline constexpr size_t kMaxExpressionBytes = 256;
inline constexpr size_t kMaxExpressionNodes = 128;
inline constexpr int kMaxExpressionDepth = 32;

// A unit the user may type after a number; `factor` converts it to the stored unit.
struct UnitConversion {
  std::string suffix;
  double factor = 1.0;
};

struct ExpressionOptions {
  std::span<const UnitConversion> units;  // matched case-insensitively (ASCII), longest first
  bool allowMixed = true;
};

class Expression {
 public:
  // The value with `mixed` substituted for the Mixed token; NaN when an operation is not finite for
  // this input (division by zero, overflow).
  double evaluate(double mixed = 0.0) const;
  bool usesMixed() const { return usesMixed_; }

 private:
  friend class ExpressionParser;
  enum class Kind : unsigned char { Number, Mixed, Negate, Add, Subtract, Multiply, Divide };
  struct Node {
    Kind kind = Kind::Number;
    double value = 0.0;
    int left = -1;
    int right = -1;
  };
  double eval(int index, double mixed) const;

  std::vector<Node> nodes_;
  int root_ = -1;
  bool usesMixed_ = false;
};

// Parses `text` (surrounding blanks ignored); nullopt when it is not a valid expression. Without a
// Mixed token the expression must evaluate to a finite number.
std::optional<Expression> parseExpression(std::string_view text, const ExpressionOptions& options = {});

}  // namespace r1ui::props
