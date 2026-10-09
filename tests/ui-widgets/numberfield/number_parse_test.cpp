// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: unit oracle for NumberParse: plain numbers, signs, exponents, arithmetic with precedence and
//   parentheses, units (case-insensitive, longest first, word boundaries), the Mixed token (evaluated
//   per object), the number formatter (digit limits, negative zero, huge magnitudes), and hostile
//   text: empty, NaN / inf / hex words, locale separators, overflow, division by zero, over-long input,
//   very deep nesting, very many nodes, non-ASCII digits.
// Callers: CTest (numberfield fast, no GPU).
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "r1ui/widgets/numberfield/NumberParse.h"

namespace {

using namespace r1ui::widgets;

bool near(std::optional<double> v, double expected) { return v && std::fabs(*v - expected) < 1e-9; }

void testPlainNumbers() {
  R1_EXPECT(near(parseNumber("12"), 12));
  R1_EXPECT(near(parseNumber("  12  "), 12));
  R1_EXPECT(near(parseNumber("\t7\t"), 7));
  R1_EXPECT(near(parseNumber("-3.5"), -3.5));
  R1_EXPECT(near(parseNumber("+4"), 4));
  R1_EXPECT(near(parseNumber(".5"), 0.5));
  R1_EXPECT(near(parseNumber("5."), 5));
  R1_EXPECT(near(parseNumber("1e3"), 1000));
  R1_EXPECT(near(parseNumber("1E-2"), 0.01));
  R1_EXPECT(near(parseNumber("2.5e+1"), 25));
  R1_EXPECT(near(parseNumber("--5"), 5));
  R1_EXPECT(near(parseNumber("\xE2\x88\x92" "5"), -5));  // U+2212 minus sign
  R1_EXPECT(near(parseNumber("0.1"), 0.1));
  R1_EXPECT(near(parseNumber("000123"), 123));
}

void testArithmetic() {
  R1_EXPECT(near(parseNumber("2*3+4"), 10));
  R1_EXPECT(near(parseNumber("2+3*4"), 14));
  R1_EXPECT(near(parseNumber("(1+2)*3"), 9));
  R1_EXPECT(near(parseNumber("10/4"), 2.5));
  R1_EXPECT(near(parseNumber("10 - 2 - 3"), 5));      // left associative
  R1_EXPECT(near(parseNumber("100 / 10 / 5"), 2));
  R1_EXPECT(near(parseNumber("-(2+3)"), -5));
  R1_EXPECT(near(parseNumber("2*-3"), -6));
  R1_EXPECT(near(parseNumber("  ( 1 + 1 ) * ( 2 + 2 ) "), 8));
}

void testUnits() {
  const std::vector<NumberUnit> units = {{"cm", 0.01}, {"mm", 0.001}, {"m", 1.0}, {"%", 1.0}, {"deg", 1.0}};
  NumberParseOptions options;
  options.units = units;
  R1_EXPECT(near(parseNumber("2cm", options), 0.02));
  R1_EXPECT(near(parseNumber("2 CM", options), 0.02));
  R1_EXPECT(near(parseNumber("3mm", options), 0.003));  // "mm" wins over "m"
  R1_EXPECT(near(parseNumber("3m", options), 3));
  R1_EXPECT(near(parseNumber("50%", options), 50));
  R1_EXPECT(near(parseNumber("45 deg", options), 45));
  R1_EXPECT(near(parseNumber("1m+2cm", options), 1.02));
  R1_EXPECT(!parseNumber("5 meters", options));  // a word, not a unit
  R1_EXPECT(!parseNumber("5 xyz", options));
  R1_EXPECT(!parseNumber("5cm", {}));            // no units configured
}

void testMixedToken() {
  NumberParseOptions mixed;
  mixed.allowMixedToken = true;
  const auto plus = parseNumberExpression("Mixed+5", mixed);
  R1_EXPECT(plus && plus->usesMixed() && near(plus->evaluate(10), 15) && near(plus->evaluate(-3), 2));
  const auto times = parseNumberExpression("mixed * 2 - 1", mixed);
  R1_EXPECT(times && near(times->evaluate(4), 7));
  const auto alone = parseNumberExpression("MIXED", mixed);
  R1_EXPECT(alone && near(alone->evaluate(42), 42));
  const auto divide = parseNumberExpression("Mixed/0", mixed);
  R1_EXPECT(divide && std::isnan(divide->evaluate(5)));  // per-object failure, reported as NaN
  const auto plain = parseNumberExpression("12", mixed);
  R1_EXPECT(plain && !plain->usesMixed());
  R1_EXPECT(!parseNumberExpression("Mixedx+1", mixed));
  R1_EXPECT(!parseNumberExpression("Mixed+", mixed));
  R1_EXPECT(!parseNumberExpression("Mixed", {}));   // not allowed unless the field is mixed
  R1_EXPECT(!parseNumber("Mixed+5", mixed));        // parseNumber is for constants only
}

void testRejectedText() {
  for (const char* text : {"", " ", "\t", "abc", "1,5", "1 2", "5 5", "()", "(", ")", "1+", "*2", "2**3", "1..2", ".", "-", "+", "e5", "1e", "1e+", "nan", "NaN",
                           "inf", "-inf", "infinity", "0x10", "1_000", "1e999", "-1e999", "1/0", "0/0", "1 + (2", "1) + (2", "\xD9\xA1\xD9\xA2",  // Arabic-Indic digits
                           "\xEF\xBC\x91",                                                                                                              // full-width 1
                           "\xFF\xFE", "5\xFF", "1;2"}) {
    R1_EXPECT(!parseNumber(text));
  }
  // Embedded NUL and control characters.
  R1_EXPECT(!parseNumber(std::string("1\0" "2", 3)));
  R1_EXPECT(!parseNumber("1\n+2"));
}

void testLimits() {
  // Too long.
  R1_EXPECT(!parseNumber(std::string(kMaxExpressionBytes + 1, '1')));
  R1_EXPECT(!parseNumber(std::string(100000, '9')));
  // The longest allowed plain digit string still parses (and is huge but finite).
  R1_EXPECT(parseNumber(std::string(kMaxExpressionBytes, '1')).has_value());
  // Depth.
  std::string deep;
  for (int i = 0; i < 20; ++i) deep += '(';
  deep += '1';
  for (int i = 0; i < 20; ++i) deep += ')';
  R1_EXPECT(near(parseNumber(deep), 1));
  std::string tooDeep;
  for (int i = 0; i < 100; ++i) tooDeep += '(';
  tooDeep += '1';
  for (int i = 0; i < 100; ++i) tooDeep += ')';
  R1_EXPECT(!parseNumber(tooDeep));
  std::string signs(200, '-');
  signs += '1';
  R1_EXPECT(!parseNumber(signs));
  // A long chain of terms within the byte limit evaluates (the node limit never trips before the byte limit).
  std::string many = "1";
  for (int i = 0; i < 99; ++i) many += "+1";
  R1_EXPECT(near(parseNumber(many), 100));
}

void testFormat() {
  R1_EXPECT(formatNumber(141, 0, 3) == "141");
  R1_EXPECT(formatNumber(1.5, 0, 3) == "1.5");
  R1_EXPECT(formatNumber(1.23456, 0, 3) == "1.235");
  R1_EXPECT(formatNumber(0.1 + 0.2, 0, 3) == "0.3");
  R1_EXPECT(formatNumber(-0.0004, 0, 3) == "0");
  R1_EXPECT(formatNumber(-0.001, 2, 2) == "0.00");  // no negative zero after rounding
  R1_EXPECT(formatNumber(-0.5, 2, 2) == "-0.50");
  R1_EXPECT(formatNumber(-0.0, 0, 3) == "0");
  R1_EXPECT(formatNumber(2, 1, 3) == "2.0");
  R1_EXPECT(formatNumber(2.5, 2, 3) == "2.50");
  R1_EXPECT(formatNumber(-12.5, 0, 0) == "-12" || formatNumber(-12.5, 0, 0) == "-13");  // round-half-even or away
  R1_EXPECT(formatNumber(100, 0, 20) == "100");     // digit limit is clamped
  R1_EXPECT(formatNumber(1e300, 0, 3).find("e+") != std::string::npos);
  R1_EXPECT(formatNumber(1e15, 0, 3) == "1e+15");
  R1_EXPECT(formatNumber(999999999999999.0, 0, 3) == "999999999999999");
  R1_EXPECT(formatNumber(std::numeric_limits<double>::quiet_NaN(), 0, 3).empty());
  R1_EXPECT(formatNumber(std::numeric_limits<double>::infinity(), 0, 3).empty());
  // A formatted value parses back to itself within the digits shown.
  for (const double v : {0.0, 1.0, -7.25, 123.456, 1e-3, 99999.999}) {
    const auto back = parseNumber(formatNumber(v, 0, 6));
    R1_EXPECT(back && std::fabs(*back - v) < 1e-6);
  }
}

}  // namespace

int main() {
  testPlainNumbers();
  testArithmetic();
  testUnits();
  testMixedToken();
  testRejectedText();
  testLimits();
  testFormat();
  return r1test::finish();
}
