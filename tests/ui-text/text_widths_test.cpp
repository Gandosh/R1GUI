// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the oracle comparing our shaped widths with the browser-measured widths in
//   tests/reference/text-widths.json (24 strings x sizes 10..16 x weights 400..700).
// Why: the UI must lay out text exactly as the reference does. The reference loads only the
//   Regular face and fakes heavier weights, so every weight is compared against our Regular
//   shaping (fake bold does not change advances).
// Callers: CTest (label fast). Prints the maximum and mean absolute difference per size.
// Tolerance: 0.01 px per string at every size (measured difference is 0; the task required 0.6 px
//   at 12-13 px, the tighter bound catches any shaping regression).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "TestSupport.h"
#include "r1ui/core/Json.h"
#include "r1ui/text/Shaper.h"

using namespace r1ui::text;
using namespace r1ui::text::testing;

namespace {

constexpr double kTolerance = 0.01;

struct Stats {
  double maxDiff = 0;
  double sumDiff = 0;
  int count = 0;
  std::string worst;
};

}  // namespace

int main() {
  FontLibrary lib;
  const FontHandle regular = loadInter(lib, "Inter-Regular.ttf", 400);

  const auto bytes = readFileBytes(std::string(R1UI_REFERENCE_DIR) + "/text-widths.json");
  expect(!bytes.empty(), "tests/reference/text-widths.json is present");
  if (bytes.empty()) return finish("text_widths");
  const auto parsed = r1ui::core::parseJson(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  expect(parsed.ok(), "reference JSON parses");
  if (!parsed.ok()) return finish("text_widths");
  const r1ui::core::JsonValue* rows = parsed.value->find("rows");
  expect(rows != nullptr && rows->size() == 576, "576 reference rows");
  if (rows == nullptr) return finish("text_widths");

  std::map<int, Stats> bySize;
  Stats overall;
  Stats focus;  // sizes 11, 12, 13
  for (std::size_t i = 0; i < rows->size(); ++i) {
    const r1ui::core::JsonValue& row = rows->child(i);
    const int size = static_cast<int>(row.find("size")->numberValue());
    const std::string text = row.find("text")->stringValue();
    const double expected = row.find("width")->numberValue();
    auto r = measureWidth(*regular, static_cast<float>(size), text);
    expect(r.ok(), "measure succeeds");
    if (!r.ok()) continue;
    const double diff = std::abs(static_cast<double>(r.value()) - expected);
    for (Stats* s : {&bySize[size], &overall}) {
      s->maxDiff = std::max(s->maxDiff, diff);
      s->sumDiff += diff;
      ++s->count;
    }
    if (diff == bySize[size].maxDiff) bySize[size].worst = text;
    if (size >= 11 && size <= 13) {
      focus.maxDiff = std::max(focus.maxDiff, diff);
      focus.sumDiff += diff;
      ++focus.count;
    }
    if (diff > kTolerance) {
      std::fprintf(stderr, "  over tolerance: %d px \"%s\" ours %.6f ref %.6f\n", size, text.c_str(),
                   static_cast<double>(r.value()), expected);
    }
  }

  for (const auto& [size, s] : bySize) {
    std::printf("size %2d: n=%3d max |diff| %.6f px, mean %.6f px (worst: \"%s\")\n", size, s.count, s.maxDiff,
                s.sumDiff / s.count, s.worst.c_str());
    expect(s.maxDiff <= kTolerance, "per-string width within tolerance");
  }
  std::printf("sizes 11-13: max %.6f px, mean %.6f px over %d rows\n", focus.maxDiff, focus.sumDiff / focus.count,
              focus.count);
  std::printf("all rows:    max %.6f px, mean %.6f px over %d rows\n", overall.maxDiff, overall.sumDiff / overall.count,
              overall.count);
  return finish("text_widths");
}
