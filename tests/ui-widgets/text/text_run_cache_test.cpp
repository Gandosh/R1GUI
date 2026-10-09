// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: regression tests for the shaped-run cache of TextEngine (phase 4 review M16): a cache hit
//   allocates no key, measureTransient() leaves the cache alone, and the cache is bounded by bytes of
//   text as well as by entries.
// Callers: CTest (fast tier). Calls: TextEngine over in-memory textures.
#include <string>

#include "TestSupport.h"

using namespace r1ui::widgets;

int main() {
  r1test::TestUi t;
  TextEngine& text = t.ui.text();
  const size_t base = text.cachedRuns();

  // The same string measured many times is one entry.
  const float w = text.measure("A reasonably long label text", 12.0f);
  for (int i = 0; i < 100; ++i) R1_EXPECT(text.measure("A reasonably long label text", 12.0f) == w);
  R1_EXPECT(text.cachedRuns() == base + 1);

  // Probing substrings (wrapping) does not fill the cache, and gives the same widths.
  const std::string probe = "Environment_Lighting_Preset_Variation_final";
  for (size_t n = 1; n <= probe.size(); ++n) {
    const std::string_view sub = std::string_view(probe).substr(0, n);
    const float transient = text.measureTransient(sub, 12.0f);
    R1_EXPECT(transient == text.measure(sub, 12.0f));
  }
  const size_t afterMeasure = text.cachedRuns();
  for (size_t n = 1; n <= probe.size(); ++n) text.measureTransient(std::string_view(probe).substr(0, n), 11.5f);
  R1_EXPECT(text.cachedRuns() == afterMeasure);  // a size nobody measured: probing cached nothing
  R1_EXPECT(text.measureTransient("", 12.0f) == 0.0f);

  // A few huge strings do not pin megabytes: the byte bound clears the cache before the entry bound would.
  text.setRunCacheLimits(4096, 20000);  // the production bound is 4 MiB; a small one keeps the test fast
  const std::string big(1000, 'x');
  const size_t small = text.cachedRuns();
  size_t previous = small;
  bool cleared = false;
  for (int i = 0; i < 100; ++i) {
    text.measure(big + std::to_string(i), 12.0f);
    const size_t now = text.cachedRuns();
    if (now < previous) cleared = true;
    R1_EXPECT(now <= small + 21);  // 20000 bytes hold at most 20 of these strings
    previous = now;
  }
  R1_EXPECT(cleared);
  return r1test::finish();
}
