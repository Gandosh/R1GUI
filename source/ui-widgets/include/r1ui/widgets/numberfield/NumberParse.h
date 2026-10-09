// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: reading and writing numbers for NumberField: a small arithmetic expression parser (+ - * /,
//   parentheses, unary signs, optional unit suffixes, optional "Mixed" token) and the number
//   formatter. Pure functions, no widget or UI dependency, so they are tested exhaustively.
// Why: spec 09 rules 12-19 and 41: typed text is parsed on commit, can carry units, and a mixed field
//   accepts an expression over the placeholder ("Mixed + 5") evaluated per object.
// Callers: NumberField, tests.
// Boundary rules (the text comes from a user or the clipboard): at most kMaxExpressionBytes bytes,
//   at most kMaxNodes nodes and kMaxDepth nested parentheses or unary signs; only ASCII digits, '.',
//   exponents, the operators and (as a convenience) U+2212 are understood; "nan", "inf", hex and
//   locale separators are rejected; a result that is not finite is rejected (division by zero, 1e999).
//   Every failure returns an empty optional and never throws.
// Grammar: expr = term {("+"|"-") term}; term = unary {("*"|"/") unary}; unary = {"+"|"-"} primary;
//   primary = number [unit] | "Mixed" | "(" expr ")". Units apply to the number literal they follow.
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace r1ui::widgets {

inline constexpr size_t kMaxExpressionBytes = 256;

// A unit the user may type after a number; `factor` converts it to the stored unit.
struct NumberUnit {
  std::string suffix;
  double factor = 1.0;
};

struct NumberParseOptions {
  std::span<const NumberUnit> units;  // matched case-insensitively (ASCII), longest first
  bool allowMixedToken = false;       // the word "Mixed" (any case) stands for the shared value
};

class NumberExpression {
 public:
  // Value of the expression with `mixed` substituted for the Mixed token; NaN when an operation is
  // not finite for this input (division by zero).
  double evaluate(double mixed = 0.0) const;
  bool usesMixed() const { return usesMixed_; }

 private:
  friend std::optional<NumberExpression> parseNumberExpression(std::string_view, const NumberParseOptions&);
  class Builder;  // the recursive-descent parser (NumberParse.cpp)
  enum class Kind : unsigned char { Number, Mixed, Negate, Add, Subtract, Multiply, Divide };
  struct Node {
    Kind kind = Kind::Number;
    double value = 0.0;
    int left = -1;
    int right = -1;
  };
  double eval(int index, double mixed, int depth) const;

  std::vector<Node> nodes_;
  int root_ = -1;
  bool usesMixed_ = false;
};

// Parses `text` (surrounding blanks ignored). Empty when the text is not a valid expression or when
// it contains no Mixed token and does not evaluate to a finite number.
std::optional<NumberExpression> parseNumberExpression(std::string_view text, const NumberParseOptions& options = {});

// Convenience: the value of a constant expression (no Mixed token), or empty.
std::optional<double> parseNumber(std::string_view text, const NumberParseOptions& options = {});

// Fixed notation with between minFractionDigits and maxFractionDigits (0..9) digits after the point:
// trailing zeros beyond the minimum are removed, "-0" becomes "0", magnitudes from 1e15 use the
// shortest round-trip form. Non-finite input gives an empty string.
std::string formatNumber(double value, int minFractionDigits, int maxFractionDigits);

}  // namespace r1ui::widgets
