// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the icon rasteriser's match against Chrome's renderings of the same SVGs
//   (tests/reference/icons/<size>/<name>.png, white on black so brightness is coverage), measured
//   with the in-process imgdiff port. It prints, for each reference size and both anti-aliasing
//   modes, the maximum and mean channel difference over the 73 icons and how many icons leave the
//   provisional "icons" profile of tests/reference/tolerance.json.
// Why: icons are drawn as tinted coverage, so coverage equal to the browser's is what makes them
//   look like the reference. The reference is not one rasteriser: Chrome draws simple shapes
//   (circle, rect) with analytic coverage and paths with 4-sample multisampling (coverage steps of
//   64), so no single mode matches pixel for pixel; AntiAlias::Msaa4 matches best (mean ~3.6 of 255
//   at 16 px) and is what visual tests use. The provisional profile (32 channels, 2% of pixels) is
//   exceeded by most icons in every mode (one 64-step sample difference on an edge pixel is
//   enough), which is reported here and not hidden; the assertions below are the calibrated bounds
//   this slice measured (recorded in docs/dev/widgets.md), not the provisional profile.
// Also checks the custom icon that replaces a Lucide icon in the reference (apply-variable).
// Callers: CTest (label fast, no GPU).
#include <fstream>
#include <iterator>

#include "TestSupport.h"
#include "r1ui/core/Json.h"
#include "r1ui/widgets/icons/SvgRaster.h"
#include "r1ui/widgets/image/ImageDiff.h"

namespace {

using namespace r1ui::widgets;

std::string readFile(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Mean difference allowed over all icons / for the worst icon, per mode (calibrated 2026-10-09: the
// measured values are 2.9..4.7 and 7.6..16.3 for Msaa4, 3.8..7.0 and 8.5..20.7 for Smooth).
struct Bounds {
  double meanOfMeans;
  double worstMean;
};

void measure(int size, AntiAlias aa, const Bounds& bounds) {
  const auto manifest = r1ui::core::parseJson(readFile(r1test::referenceDir() / "icons" / "manifest.json"));
  R1_EXPECT(manifest.ok());
  const auto* names = manifest.value->find("icons");
  R1_EXPECT(names != nullptr);
  std::string error;
  const auto tolerance = image::loadTolerance(r1test::referenceDir() / "tolerance.json", "icons", error);
  R1_EXPECT(tolerance.has_value());
  if (names == nullptr || !tolerance) return;

  int count = 0;
  int outsideProfile = 0;
  int maxDiff = 0;
  double meanSum = 0.0;
  double worstMean = 0.0;
  std::string worstName;
  for (size_t i = 0; i < names->size(); ++i) {
    const std::string& name = names->child(i).stringValue();
    const auto svg = parseSvg(readFile(r1test::assetsDir() / "icons" / "lucide" / (name + ".svg")));
    R1_EXPECT(svg.ok);
    const auto ref = image::loadPng(r1test::referenceDir() / "icons" / std::to_string(size) / (name + ".png"));
    R1_EXPECT(ref.ok());
    if (!svg.ok || !ref.ok()) continue;
    const std::vector<uint8_t> coverage = rasterizeIcon(svg.icon, size, aa);
    image::Image candidate;
    candidate.width = candidate.height = static_cast<uint32_t>(size);
    candidate.rgba.resize(coverage.size() * 4);
    for (size_t p = 0; p < coverage.size(); ++p) {
      candidate.rgba[p * 4] = candidate.rgba[p * 4 + 1] = candidate.rgba[p * 4 + 2] = coverage[p];
      candidate.rgba[p * 4 + 3] = 255;
    }
    const image::DiffMetrics m = image::compare(*ref.image, candidate, *tolerance);
    R1_EXPECT(m.error.empty());
    ++count;
    if (!m.pass) ++outsideProfile;
    maxDiff = std::max(maxDiff, m.maxChannelDiff);
    meanSum += m.meanDiff;
    if (m.meanDiff > worstMean) {
      worstMean = m.meanDiff;
      worstName = name;
    }
  }
  const double meanOfMeans = count != 0 ? meanSum / count : 0.0;
  std::printf("icons %s @%2d px: %d icons, max channel diff %d, mean diff %.3f (worst mean %.3f: %s), outside the provisional icons profile: %d\n",
              aa == AntiAlias::Msaa4 ? "msaa4 " : "smooth", size, count, maxDiff, meanOfMeans, worstMean, worstName.c_str(), outsideProfile);
  R1_EXPECT(count == 73);
  R1_EXPECT(meanOfMeans <= bounds.meanOfMeans);
  R1_EXPECT(worstMean <= bounds.worstMean);
}

void testCustomIcons() {
  const auto svg = parseSvg(readFile(r1test::assetsDir() / "icons" / "custom" / "apply-variable.svg"));
  R1_EXPECT(svg.ok);
  if (!svg.ok) return;
  const std::vector<uint8_t> cov = rasterizeIcon(svg.icon, 14);
  // Nested diamond: ink on the outline and on a small inner diamond around the centre (the reference
  // shows a ring, not a solid dot), empty between them.
  const auto at = [&](int x, int y) { return cov[static_cast<size_t>(y) * 14 + static_cast<size_t>(x)]; };
  R1_EXPECT(at(7, 7) > 100 || at(6, 6) > 100);  // inner diamond at the centre
  R1_EXPECT(at(0, 7) > 40 || at(1, 7) > 40);    // left tip of the outer diamond
  R1_EXPECT(at(7, 3) < 80 && at(3, 7) < 80);    // gap between the outline and the centre
}

}  // namespace

int main() {
  for (const int size : {12, 14, 16, 24}) {
    measure(size, AntiAlias::Smooth, {7.5, 22.0});
    measure(size, AntiAlias::Msaa4, {5.0, 17.5});
  }
  testCustomIcons();
  return r1test::finish();
}
