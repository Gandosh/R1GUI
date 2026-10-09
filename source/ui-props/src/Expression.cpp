// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the recursive-descent parser and the evaluator behind Expression.h.
// Why: see Expression.h. Nesting is bounded at parse time, so evaluation recursion is bounded too.
// Callers: PropertyContext*.cpp, tests.
#include "r1ui/props/Expression.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>

namespace r1ui::props {

// Parser state lives in a named class so Expression can befriend it without exposing its members.
class ExpressionParser {
 public:
  ExpressionParser(std::string_view text, const ExpressionOptions& options, Expression& out) : text_(text), options_(options), out_(out) {
    sortedUnits_.assign(options.units.begin(), options.units.end());
    std::sort(sortedUnits_.begin(), sortedUnits_.end(), [](const UnitConversion& a, const UnitConversion& b) { return a.suffix.size() > b.suffix.size(); });
  }

  bool parse() {
    if (text_.size() > kMaxExpressionBytes) return false;
    const int root = expression(0);
    skipBlanks();
    if (root < 0 || pos_ != text_.size()) return false;
    out_.root_ = root;
    return true;
  }

 private:
  using Kind = Expression::Kind;

  int add(Kind kind, double value = 0.0, int left = -1, int right = -1) {
    if (out_.nodes_.size() >= kMaxExpressionNodes) return -1;
    out_.nodes_.push_back({kind, value, left, right});
    return static_cast<int>(out_.nodes_.size() - 1);
  }

  void skipBlanks() {
    while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t')) ++pos_;
  }

  // U+2212 (minus sign) is accepted wherever '-' is.
  bool consumeMinus() {
    if (pos_ < text_.size() && text_[pos_] == '-') {
      ++pos_;
      return true;
    }
    if (text_.compare(pos_, 3, "\xE2\x88\x92") == 0) {
      pos_ += 3;
      return true;
    }
    return false;
  }

  int expression(int depth) {
    if (depth > kMaxExpressionDepth) return -1;
    int left = term(depth);
    while (left >= 0) {
      skipBlanks();
      Kind kind;
      if (pos_ < text_.size() && text_[pos_] == '+') {
        ++pos_;
        kind = Kind::Add;
      } else if (consumeMinus()) {
        kind = Kind::Subtract;
      } else {
        break;
      }
      const int right = term(depth);
      left = right < 0 ? -1 : add(kind, 0.0, left, right);
    }
    return left;
  }

  int term(int depth) {
    int left = unary(depth);
    while (left >= 0) {
      skipBlanks();
      Kind kind;
      if (pos_ < text_.size() && text_[pos_] == '*') {
        kind = Kind::Multiply;
      } else if (pos_ < text_.size() && text_[pos_] == '/') {
        kind = Kind::Divide;
      } else {
        break;
      }
      ++pos_;
      const int right = unary(depth);
      left = right < 0 ? -1 : add(kind, 0.0, left, right);
    }
    return left;
  }

  int unary(int depth) {
    if (depth > kMaxExpressionDepth) return -1;
    skipBlanks();
    if (pos_ < text_.size() && text_[pos_] == '+') {
      ++pos_;
      return unary(depth + 1);
    }
    if (consumeMinus()) {
      const int operand = unary(depth + 1);
      return operand < 0 ? -1 : add(Kind::Negate, 0.0, operand);
    }
    return primary(depth);
  }

  static bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
  static char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

  bool matchesWord(std::string_view word) const {
    if (text_.size() - pos_ < word.size()) return false;
    for (size_t i = 0; i < word.size(); ++i) {
      if (lower(text_[pos_ + i]) != lower(word[i])) return false;
    }
    const size_t after = pos_ + word.size();
    return after >= text_.size() || !isLetter(text_[after]);
  }

  int primary(int depth) {
    skipBlanks();
    if (pos_ >= text_.size()) return -1;
    const char c = text_[pos_];
    if (c == '(') {
      ++pos_;
      const int inner = expression(depth + 1);
      skipBlanks();
      if (inner < 0 || pos_ >= text_.size() || text_[pos_] != ')') return -1;
      ++pos_;
      return inner;
    }
    if (options_.allowMixed && matchesWord("mixed")) {
      pos_ += 5;
      out_.usesMixed_ = true;
      return add(Kind::Mixed);
    }
    return number();
  }

  int number() {
    const size_t start = pos_;
    size_t i = pos_;
    while (i < text_.size() && text_[i] >= '0' && text_[i] <= '9') ++i;
    if (i < text_.size() && text_[i] == '.') {
      ++i;
      while (i < text_.size() && text_[i] >= '0' && text_[i] <= '9') ++i;
    }
    if (i == start || (i == start + 1 && text_[start] == '.')) return -1;
    // An exponent needs digits; otherwise the 'e' belongs to a unit suffix.
    if (i < text_.size() && (text_[i] == 'e' || text_[i] == 'E')) {
      size_t j = i + 1;
      if (j < text_.size() && (text_[j] == '+' || text_[j] == '-')) ++j;
      if (j < text_.size() && text_[j] >= '0' && text_[j] <= '9') {
        while (j < text_.size() && text_[j] >= '0' && text_[j] <= '9') ++j;
        i = j;
      }
    }
    double value = 0.0;
    const auto [end, ec] = std::from_chars(text_.data() + start, text_.data() + i, value);
    if (ec != std::errc() || end != text_.data() + i || !std::isfinite(value)) return -1;
    pos_ = i;
    value *= unitFactor();
    if (!std::isfinite(value)) return -1;
    return add(Kind::Number, value);
  }

  // The factor of a unit suffix after the number (1 when there is none).
  double unitFactor() {
    size_t probe = pos_;
    while (probe < text_.size() && text_[probe] == ' ') ++probe;
    for (const UnitConversion& unit : sortedUnits_) {
      if (unit.suffix.empty() || text_.size() - probe < unit.suffix.size()) continue;
      bool match = true;
      for (size_t k = 0; k < unit.suffix.size(); ++k) {
        if (lower(text_[probe + k]) != lower(unit.suffix[k])) {
          match = false;
          break;
        }
      }
      if (!match) continue;
      const size_t after = probe + unit.suffix.size();
      if (isLetter(unit.suffix.back()) && after < text_.size() && isLetter(text_[after])) continue;
      pos_ = after;
      return unit.factor;
    }
    return 1.0;
  }

  std::string_view text_;
  const ExpressionOptions& options_;
  Expression& out_;
  std::vector<UnitConversion> sortedUnits_;
  size_t pos_ = 0;
};

double Expression::eval(int index, double mixed) const {
  const Node& n = nodes_[static_cast<size_t>(index)];
  switch (n.kind) {
    case Kind::Number: return n.value;
    case Kind::Mixed: return mixed;
    case Kind::Negate: return -eval(n.left, mixed);
    case Kind::Add: return eval(n.left, mixed) + eval(n.right, mixed);
    case Kind::Subtract: return eval(n.left, mixed) - eval(n.right, mixed);
    case Kind::Multiply: return eval(n.left, mixed) * eval(n.right, mixed);
    case Kind::Divide: {
      const double divisor = eval(n.right, mixed);
      return divisor == 0.0 ? std::nan("") : eval(n.left, mixed) / divisor;
    }
  }
  return std::nan("");
}

double Expression::evaluate(double mixed) const {
  if (root_ < 0) return std::nan("");
  const double result = eval(root_, mixed);
  return std::isfinite(result) ? result : std::nan("");
}

std::optional<Expression> parseExpression(std::string_view text, const ExpressionOptions& options) {
  Expression expression;
  ExpressionParser parser(text, options, expression);
  if (!parser.parse()) return std::nullopt;
  if (!expression.usesMixed() && !std::isfinite(expression.evaluate())) return std::nullopt;
  return expression;
}

}  // namespace r1ui::props
