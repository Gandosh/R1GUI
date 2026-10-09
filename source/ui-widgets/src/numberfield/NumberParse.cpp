// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of NumberParse.h (parser, evaluator, formatter).
// Invariants: the parser consumes the whole text or fails; node and depth limits bound memory and
//   recursion for any input; evaluation recursion is bounded by the node count.
// Callers: NumberField, tests.
#include "r1ui/widgets/numberfield/NumberParse.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>

namespace r1ui::widgets {

namespace {

constexpr size_t kMaxNodes = 256;
constexpr int kMaxDepth = 32;

bool isBlank(char c) { return c == ' ' || c == '\t'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// U+2212 (minus sign) is typed by some keyboards and pasted from documents; it means '-'.
std::string normalise(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (i + 2 < text.size() && static_cast<unsigned char>(text[i]) == 0xE2 && static_cast<unsigned char>(text[i + 1]) == 0x88 &&
        static_cast<unsigned char>(text[i + 2]) == 0x92) {
      out.push_back('-');
      i += 2;
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

}  // namespace

// ---- parser -----------------------------------------------------------------------------------

class NumberExpression::Builder {
 public:
  Builder(std::string_view text, const NumberParseOptions& options, NumberExpression& out) : text_(text), options_(options), out_(out) {
    order_.resize(options.units.size());
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    std::sort(order_.begin(), order_.end(), [&](size_t a, size_t b) { return options.units[a].suffix.size() > options.units[b].suffix.size(); });
  }

  bool run() {
    skipBlanks();
    if (atEnd()) return false;
    const int root = expression(0);
    if (root < 0) return false;
    skipBlanks();
    if (!atEnd()) return false;
    out_.root_ = root;
    return true;
  }

 private:
  bool atEnd() const { return pos_ >= text_.size(); }
  char peek() const { return atEnd() ? '\0' : text_[pos_]; }
  void skipBlanks() {
    while (!atEnd() && isBlank(text_[pos_])) ++pos_;
  }

  int add(Kind kind, double value = 0.0, int left = -1, int right = -1) {
    if (out_.nodes_.size() >= kMaxNodes) return -1;
    out_.nodes_.push_back({kind, value, left, right});
    return static_cast<int>(out_.nodes_.size() - 1);
  }

  int expression(int depth) {
    if (depth > kMaxDepth) return -1;
    int left = term(depth);
    while (left >= 0) {
      skipBlanks();
      const char c = peek();
      if (c != '+' && c != '-') break;
      ++pos_;
      const int right = term(depth);
      if (right < 0) return -1;
      left = add(c == '+' ? Kind::Add : Kind::Subtract, 0.0, left, right);
    }
    return left;
  }

  int term(int depth) {
    int left = unary(depth);
    while (left >= 0) {
      skipBlanks();
      const char c = peek();
      if (c != '*' && c != '/') break;
      ++pos_;
      const int right = unary(depth);
      if (right < 0) return -1;
      left = add(c == '*' ? Kind::Multiply : Kind::Divide, 0.0, left, right);
    }
    return left;
  }

  int unary(int depth) {
    if (depth > kMaxDepth) return -1;
    skipBlanks();
    const char c = peek();
    if (c == '+') {
      ++pos_;
      return unary(depth + 1);
    }
    if (c == '-') {
      ++pos_;
      const int operand = unary(depth + 1);
      return operand < 0 ? -1 : add(Kind::Negate, 0.0, operand);
    }
    return primary(depth);
  }

  int primary(int depth) {
    skipBlanks();
    const char c = peek();
    if (c == '(') {
      ++pos_;
      const int inner = expression(depth + 1);
      if (inner < 0) return -1;
      skipBlanks();
      if (peek() != ')') return -1;
      ++pos_;
      return inner;
    }
    if (options_.allowMixedToken && matchWord("mixed")) {
      out_.usesMixed_ = true;
      return add(Kind::Mixed);
    }
    return number();
  }

  // True (and consumed) when the text continues with `word` (ASCII, any case) not followed by a letter.
  bool matchWord(std::string_view word) {
    if (text_.size() - pos_ < word.size()) return false;
    for (size_t i = 0; i < word.size(); ++i) {
      if (lower(text_[pos_ + i]) != word[i]) return false;
    }
    const size_t after = pos_ + word.size();
    if (after < text_.size() && isAlpha(text_[after])) return false;
    pos_ = after;
    return true;
  }

  int number() {
    const size_t start = pos_;
    size_t p = pos_;
    size_t mantissaDigits = 0;
    while (p < text_.size() && isDigit(text_[p])) {
      ++p;
      ++mantissaDigits;
    }
    if (p < text_.size() && text_[p] == '.') {
      ++p;
      while (p < text_.size() && isDigit(text_[p])) {
        ++p;
        ++mantissaDigits;
      }
    }
    if (mantissaDigits == 0) return -1;
    if (p < text_.size() && (text_[p] == 'e' || text_[p] == 'E')) {
      size_t q = p + 1;
      if (q < text_.size() && (text_[q] == '+' || text_[q] == '-')) ++q;
      const size_t digitsStart = q;
      while (q < text_.size() && isDigit(text_[q])) ++q;
      if (q > digitsStart) p = q;  // otherwise the 'e' is not an exponent (it may start a unit)
    }
    double value = 0.0;
    const char* first = text_.data() + start;
    const auto [end, ec] = std::from_chars(first, text_.data() + p, value);
    if (ec != std::errc() || end != text_.data() + p || !std::isfinite(value)) return -1;
    pos_ = p;
    value *= unitFactor();
    if (!std::isfinite(value)) return -1;
    return add(Kind::Number, value);
  }

  // Consumes an optional unit suffix after a number and returns its factor (1 when none).
  double unitFactor() {
    const size_t saved = pos_;
    skipBlanks();
    for (const size_t index : order_) {
      const std::string& suffix = options_.units[index].suffix;
      if (suffix.empty() || text_.size() - pos_ < suffix.size()) continue;
      bool same = true;
      for (size_t i = 0; i < suffix.size() && same; ++i) same = lower(text_[pos_ + i]) == lower(suffix[i]);
      if (!same) continue;
      const size_t after = pos_ + suffix.size();
      const bool wordLike = isAlpha(suffix.back());
      if (wordLike && after < text_.size() && isAlpha(text_[after])) continue;
      pos_ = after;
      return options_.units[index].factor;
    }
    pos_ = saved;
    return 1.0;
  }

  std::string_view text_;
  const NumberParseOptions& options_;
  NumberExpression& out_;
  std::vector<size_t> order_;
  size_t pos_ = 0;
};

// ---- public API -----------------------------------------------------------------------------------

std::optional<NumberExpression> parseNumberExpression(std::string_view text, const NumberParseOptions& options) {
  if (text.size() > kMaxExpressionBytes) return std::nullopt;
  const std::string normal = normalise(text);
  NumberExpression expression;
  NumberExpression::Builder builder(normal, options, expression);
  if (!builder.run()) return std::nullopt;
  if (!expression.usesMixed() && !std::isfinite(expression.evaluate())) return std::nullopt;
  return expression;
}

std::optional<double> parseNumber(std::string_view text, const NumberParseOptions& options) {
  NumberParseOptions constant = options;
  constant.allowMixedToken = false;
  const std::optional<NumberExpression> expression = parseNumberExpression(text, constant);
  if (!expression) return std::nullopt;
  return expression->evaluate();
}

double NumberExpression::evaluate(double mixed) const { return root_ < 0 ? std::nan("") : eval(root_, mixed, 0); }

double NumberExpression::eval(int index, double mixed, int depth) const {
  if (index < 0 || static_cast<size_t>(index) >= nodes_.size() || depth > static_cast<int>(kMaxNodes)) return std::nan("");
  const Node& n = nodes_[static_cast<size_t>(index)];
  double result = 0.0;
  switch (n.kind) {
    case Kind::Number: return n.value;
    case Kind::Mixed: return mixed;
    case Kind::Negate: return -eval(n.left, mixed, depth + 1);
    case Kind::Add: result = eval(n.left, mixed, depth + 1) + eval(n.right, mixed, depth + 1); break;
    case Kind::Subtract: result = eval(n.left, mixed, depth + 1) - eval(n.right, mixed, depth + 1); break;
    case Kind::Multiply: result = eval(n.left, mixed, depth + 1) * eval(n.right, mixed, depth + 1); break;
    case Kind::Divide: {
      const double divisor = eval(n.right, mixed, depth + 1);
      result = divisor == 0.0 ? std::nan("") : eval(n.left, mixed, depth + 1) / divisor;
      break;
    }
  }
  return std::isfinite(result) ? result : std::nan("");
}

// ---- formatting ---------------------------------------------------------------------------------------

std::string formatNumber(double value, int minFractionDigits, int maxFractionDigits) {
  if (!std::isfinite(value)) return {};
  const int maxDigits = std::clamp(maxFractionDigits, 0, 9);
  const int minDigits = std::clamp(minFractionDigits, 0, maxDigits);
  char buffer[400];
  std::to_chars_result result{};
  if (std::fabs(value) >= 1.0e15) {
    result = std::to_chars(buffer, buffer + sizeof buffer, value);
    return result.ec == std::errc() ? std::string(buffer, result.ptr) : std::string();
  }
  result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed, maxDigits);
  if (result.ec != std::errc()) return {};
  std::string text(buffer, result.ptr);
  const size_t point = text.find('.');
  if (point != std::string::npos) {
    const size_t keep = point + 1 + static_cast<size_t>(minDigits);
    while (text.size() > keep && text.back() == '0') text.pop_back();
    if (text.back() == '.') text.pop_back();
  }
  // A small negative value that rounds to zero is "0", not "-0.00".
  if (text.size() > 1 && text[0] == '-' && text.find_first_not_of("0.", 1) == std::string::npos) text.erase(0, 1);
  return text;
}

}  // namespace r1ui::widgets
