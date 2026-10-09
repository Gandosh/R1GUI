// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: R1_EXPECT(condition, "what it is about"), the form of the expectation macro the dock tests
//   use: the optional second argument is printed with the failing condition.
// Why: dock scenarios contain many near-identical expectations; the message says which clause of the
//   spec or contract failed without reading the code.
// Callers: DockTestSupport.h and BackendConformance.h.
#pragma once

#include "TestSupport.h"

#undef R1_EXPECT
#define R1_EXPECT(cond, ...) ::r1test::report(static_cast<bool>(cond), #cond " " #__VA_ARGS__, __FILE__, __LINE__)
