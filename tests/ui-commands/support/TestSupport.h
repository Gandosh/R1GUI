// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the helpers every ui-commands test shares: expectation macros that count failures and print
//   the failing expression, and builders for small command sets.
// Why: tests are plain executables (exit code 0 = pass); a test should be a page of expectations
//   around a registry, an overrides table, a keymap and a router.
// Callers: tests/ui-commands/*_test.cpp. Usage: `int main() { ...; return r1test::finish(); }`.
// Failure behavior: expectations never abort; finish() returns 1 when any failed.
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <utility>

#include "r1ui/commands/CommandRegistry.h"

namespace r1test {

inline int& failureCount() {
  static int failures = 0;
  return failures;
}

inline void report(bool ok, const char* expression, const char* file, int line) {
  if (ok) return;
  std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
  ++failureCount();
}

inline int finish() {
  if (failureCount() != 0) {
    std::fprintf(stderr, "%d expectation(s) failed\n", failureCount());
    return 1;
  }
  std::printf("ok\n");
  return 0;
}

#define R1_EXPECT(cond) ::r1test::report(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

using namespace r1ui::commands;

// The Key enum names only A, Digit0 and F1; the other letters and function keys are their codes.
inline constexpr Key kB = static_cast<Key>('B'), kC = static_cast<Key>('C'), kD = static_cast<Key>('D'), kE = static_cast<Key>('E'),
                     kF = static_cast<Key>('F'), kG = static_cast<Key>('G'), kH = static_cast<Key>('H'), kI = static_cast<Key>('I'),
                     kJ = static_cast<Key>('J'), kK = static_cast<Key>('K'), kL = static_cast<Key>('L'), kM = static_cast<Key>('M'),
                     kN = static_cast<Key>('N'), kO = static_cast<Key>('O'), kP = static_cast<Key>('P'), kQ = static_cast<Key>('Q'),
                     kR = static_cast<Key>('R'), kS = static_cast<Key>('S'), kT = static_cast<Key>('T'), kU = static_cast<Key>('U'),
                     kV = static_cast<Key>('V'), kW = static_cast<Key>('W'), kX = static_cast<Key>('X'), kY = static_cast<Key>('Y'),
                     kZ = static_cast<Key>('Z');
inline constexpr Key kF2 = static_cast<Key>(113);
inline constexpr Key kF5 = static_cast<Key>(116);

inline KeyChord chord(Key key, uint8_t mods = 0) { return KeyChord{key, mods, false}; }
inline ChordSequence seq(Key key, uint8_t mods = 0) { return ChordSequence::single(chord(key, mods)); }
inline ChordSequence seq2(Key k1, uint8_t m1, Key k2, uint8_t m2 = 0) { return ChordSequence::pair(chord(k1, m1), chord(k2, m2)); }

// A command that counts how often it ran.
inline CommandDef makeCommand(std::string id, std::string label, std::string context = kGlobalContext, ChordSequence primary = {}, ChordSequence alternate = {}, int* counter = nullptr) {
  CommandDef def;
  def.id = std::move(id);
  def.label = std::move(label);
  def.context = std::move(context);
  def.defaultChords = {primary, alternate};
  def.execute = [counter](const ExecuteArgs&) {
    if (counter != nullptr) ++*counter;
    return ExecuteResult::handled();
  };
  return def;
}

}  // namespace r1test
