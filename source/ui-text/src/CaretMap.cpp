// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CaretMap construction from a shaped run (see r1ui/text/CaretMap.h).
// Method: glyphs sharing one cluster value form a group with an x extent. Each group's source
//   byte range is [its cluster, the next larger cluster) and is split at the grapheme boundaries
//   inside it, spreading the group's width evenly across those graphemes.
// Cost: O(glyphs log glyphs + text bytes); no per-boundary rescans.
#include "r1ui/text/CaretMap.h"

#include <algorithm>
#include <cmath>

#include "r1ui/text/Grapheme.h"

namespace r1ui::text {

namespace {

struct Group {
  std::size_t begin = 0;  // cluster value = start byte offset
  float x0 = 0;           // visual extent
  float x1 = 0;
  bool rtl = false;
};

}  // namespace

CaretMap CaretMap::build(const ShapedRun& run, std::string_view text) {
  CaretMap map;
  map.width_ = run.width;

  // Group consecutive glyphs with equal cluster values, in visual order, tracking the pen.
  std::vector<Group> groups;
  double pen = 0.0;
  for (const ShapedGlyph& g : run.glyphs) {
    const std::size_t cluster = std::min<std::size_t>(g.cluster, text.size());
    if (groups.empty() || groups.back().begin != cluster) {
      groups.push_back(Group{cluster, static_cast<float>(pen), static_cast<float>(pen), g.rtl});
    }
    pen += static_cast<double>(g.xAdvance);
    groups.back().x1 = static_cast<float>(pen);
  }
  if (groups.empty()) return map;

  // Each group owns the bytes up to the next larger cluster start (or the end of the text).
  std::vector<std::size_t> starts;
  starts.reserve(groups.size());
  for (const Group& g : groups) starts.push_back(g.begin);
  std::sort(starts.begin(), starts.end());
  starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

  std::vector<Stop> stops;
  for (const Group& g : groups) {
    const auto it = std::upper_bound(starts.begin(), starts.end(), g.begin);
    const std::size_t end = it == starts.end() ? text.size() : *it;
    const std::size_t first = nextGraphemeBoundary(text, g.begin);
    if (first >= end) {  // the common case: one grapheme per cluster, no split needed
      stops.push_back(Stop{g.begin, g.rtl ? g.x1 : g.x0});
      continue;
    }
    std::vector<std::size_t> cuts{g.begin};
    for (std::size_t b = first; b < end; b = nextGraphemeBoundary(text, b)) {
      cuts.push_back(b);
    }
    const double n = static_cast<double>(cuts.size());
    for (std::size_t i = 0; i < cuts.size(); ++i) {
      const double t = static_cast<double>(i) / n;
      const double x = g.rtl ? g.x1 - (g.x1 - g.x0) * t : g.x0 + (g.x1 - g.x0) * t;
      stops.push_back(Stop{cuts[i], static_cast<float>(x)});
    }
  }

  // Stable sort keeps the first group's stop when two groups claim the same offset.
  std::stable_sort(stops.begin(), stops.end(),
                   [](const Stop& a, const Stop& b) { return a.offset < b.offset; });
  stops.erase(std::unique(stops.begin(), stops.end(),
                          [](const Stop& a, const Stop& b) { return a.offset == b.offset; }),
              stops.end());

  if (stops.front().offset != 0) stops.insert(stops.begin(), Stop{0, 0.0f});
  if (stops.back().offset != text.size()) {
    const Group& last = *std::max_element(groups.begin(), groups.end(),
                                          [](const Group& a, const Group& b) { return a.begin < b.begin; });
    stops.push_back(Stop{text.size(), last.rtl ? last.x0 : last.x1});
  }
  map.stops_ = std::move(stops);
  return map;
}

float CaretMap::xForOffset(std::size_t offset) const {
  const auto it = std::upper_bound(stops_.begin(), stops_.end(), offset,
                                   [](std::size_t value, const Stop& s) { return value < s.offset; });
  return it == stops_.begin() ? stops_.front().x : (it - 1)->x;
}

std::size_t CaretMap::offsetForX(float x) const {
  if (std::isnan(x)) return stops_.front().offset;
  // Distances in double: with a huge x, float differences between nearby stops would collapse.
  std::size_t best = 0;
  double bestDist = std::abs(static_cast<double>(stops_[0].x) - static_cast<double>(x));
  for (std::size_t i = 1; i < stops_.size(); ++i) {
    const double d = std::abs(static_cast<double>(stops_[i].x) - static_cast<double>(x));
    if (d < bestDist) {
      best = i;
      bestDist = d;
    }
  }
  return stops_[best].offset;
}

}  // namespace r1ui::text
