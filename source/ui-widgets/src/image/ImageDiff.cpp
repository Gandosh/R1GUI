// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of ImageDiff.h (profile loading and the comparison loop).
// Callers: the visual harness and calibration tests.
#include "r1ui/widgets/image/ImageDiff.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "r1ui/core/Json.h"

namespace r1ui::widgets::image {

namespace {

void applyProfile(const core::JsonValue& object, Tolerance& tolerance) {
  if (const core::JsonValue* c = object.find("channelTolerance")) tolerance.channelTolerance = static_cast<int>(c->numberValue());
  if (const core::JsonValue* f = object.find("maxFailingFraction")) tolerance.maxFailingFraction = f->numberValue();
}

}  // namespace

std::optional<Tolerance> loadTolerance(const std::filesystem::path& toleranceFile, std::string_view profile, std::string& error) {
  std::ifstream in(toleranceFile, std::ios::binary);
  if (!in) {
    error = "cannot read " + toleranceFile.string();
    return std::nullopt;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const core::JsonResult parsed = core::parseJson(text);
  if (!parsed.ok() || !parsed.value->isObject()) {
    error = toleranceFile.string() + ": invalid JSON";
    return std::nullopt;
  }
  Tolerance tolerance;
  const core::JsonValue* def = parsed.value->find("default");
  if (def == nullptr) {
    error = toleranceFile.string() + ": no default profile";
    return std::nullopt;
  }
  applyProfile(*def, tolerance);
  if (profile != "default") {
    const core::JsonValue* profiles = parsed.value->find("profiles");
    const core::JsonValue* named = profiles != nullptr ? profiles->find(profile) : nullptr;
    if (named == nullptr) {
      error = "unknown tolerance profile \"" + std::string(profile) + "\"";
      return std::nullopt;
    }
    applyProfile(*named, tolerance);
  }
  if (tolerance.channelTolerance < 0 || !(tolerance.maxFailingFraction >= 0.0)) {
    error = "invalid tolerance values";
    return std::nullopt;
  }
  return tolerance;
}

DiffMetrics compare(const Image& reference, const Image& candidate, const Tolerance& tolerance) {
  DiffMetrics m;
  m.width = reference.width;
  m.height = reference.height;
  if (reference.width != candidate.width || reference.height != candidate.height) {
    m.error = "size mismatch: reference " + std::to_string(reference.width) + "x" + std::to_string(reference.height) +
              ", candidate " + std::to_string(candidate.width) + "x" + std::to_string(candidate.height);
    return m;
  }
  const size_t count = size_t{reference.width} * reference.height;
  if (count == 0 || reference.rgba.size() != count * 4 || candidate.rgba.size() != count * 4) {
    m.error = "empty or inconsistent image";
    return m;
  }
  m.failMask.assign(count, 0);
  uint64_t total = 0;
  int minX = static_cast<int>(reference.width);
  int minY = static_cast<int>(reference.height);
  int maxX = -1;
  int maxY = -1;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t* r = reference.rgba.data() + i * 4;
    const uint8_t* c = candidate.rgba.data() + i * 4;
    const int d = std::max({std::abs(r[0] - c[0]), std::abs(r[1] - c[1]), std::abs(r[2] - c[2])});
    if (d == 0) continue;
    total += static_cast<uint64_t>(d);
    m.maxChannelDiff = std::max(m.maxChannelDiff, d);
    if (d > tolerance.channelTolerance) {
      ++m.failingPixels;
      m.failMask[i] = 1;
      const int x = static_cast<int>(i % reference.width);
      const int y = static_cast<int>(i / reference.width);
      minX = std::min(minX, x);
      minY = std::min(minY, y);
      maxX = std::max(maxX, x);
      maxY = std::max(maxY, y);
    }
  }
  m.failingFraction = static_cast<double>(m.failingPixels) / static_cast<double>(count);
  m.meanDiff = static_cast<double>(total) / static_cast<double>(count);
  if (m.failingPixels != 0) {
    m.boundsMinX = minX;
    m.boundsMinY = minY;
    m.boundsMaxX = maxX;
    m.boundsMaxY = maxY;
  }
  m.pass = m.failingFraction <= tolerance.maxFailingFraction;
  return m;
}

Image makeDiffImage(const Image& candidate, const DiffMetrics& metrics) {
  Image out = candidate;
  const size_t count = size_t{candidate.width} * candidate.height;
  for (size_t i = 0; i < count && i < metrics.failMask.size(); ++i) {
    uint8_t* p = out.rgba.data() + i * 4;
    if (metrics.failMask[i] != 0) {
      p[0] = 255;
      p[1] = 0;
      p[2] = 0;
      p[3] = 255;
    } else {
      p[0] = static_cast<uint8_t>(p[0] / 3);
      p[1] = static_cast<uint8_t>(p[1] / 3);
      p[2] = static_cast<uint8_t>(p[2] / 3);
    }
  }
  return out;
}

std::string describe(const DiffMetrics& m) {
  if (!m.error.empty()) return "compare error: " + m.error;
  char buffer[200];
  std::snprintf(buffer, sizeof buffer, "%ux%u failing %zu (%.3f%%) max %d mean %.3f %s", m.width, m.height, m.failingPixels,
                m.failingFraction * 100.0, m.maxChannelDiff, m.meanDiff, m.pass ? "PASS" : "FAIL");
  return buffer;
}

}  // namespace r1ui::widgets::image
