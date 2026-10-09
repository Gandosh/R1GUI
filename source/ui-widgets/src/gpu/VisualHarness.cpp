// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of VisualHarness.h: the shared GPU rig, offscreen rendering of a widget in a
//   state, reference comparison and the ink measurement.
// Invariants: the rig is built once (device before services) and destroyed in reverse at exit; a
//   render never leaves input state behind (each render owns a fresh UiContext).
// Callers: visual tests, the text calibration test.
#include "r1ui/widgets/testing/VisualHarness.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>

#include "r1ui/render/OffscreenTarget.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/widgets/gpu/GpuTextures.h"

namespace r1ui::widgets::testing {

namespace {

struct Rig {
  explicit Rig(const VisualPaths& paths) : factory(device), services(loadTokens(paths), factory, makePaths(paths)) {
    // The reference crops were rendered by a browser whose GPU raster uses 4-sample multisampling for paths.
    services.icons().setAntiAlias(AntiAlias::Msaa4);
  }

  static std::shared_ptr<const theme::Tokens> loadTokens(const VisualPaths& paths) {
    auto result = theme::Tokens::loadFile(paths.assetsDir / "theme" / "tokens.json");
    if (!result.ok()) throw std::runtime_error("tokens.json: " + result.error);
    return std::make_shared<const theme::Tokens>(std::move(*result.tokens));
  }
  static ServicesPaths makePaths(const VisualPaths& paths) {
    return {paths.assetsDir / "fonts", {paths.assetsDir / "icons" / "lucide", paths.assetsDir / "icons" / "custom"}};
  }

  render::RenderDevice device;
  GpuTextureFactory factory;
  Services services;
};

std::unique_ptr<Rig>& rigStorage() {
  static std::unique_ptr<Rig> instance;
  return instance;
}

Rig* created() { return rigStorage().get(); }

Rig& rig(const VisualPaths& paths) {
  std::unique_ptr<Rig>& instance = rigStorage();
  if (!instance) instance = std::make_unique<Rig>(paths);
  return *instance;
}

render::Color clearColor(Services& services, const std::string& token) { return services.color(token); }

image::Image toImage(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h) {
  image::Image img;
  img.width = w;
  img.height = h;
  img.rgba = rgba;
  return img;
}

std::string artifactName(const VisualSpec& spec) {
  return spec.reference + (spec.theme == theme::ThemeId::Light ? "-light" : "-dark") + (spec.scale != 1.0f ? "-x" + std::to_string(static_cast<int>(std::lround(spec.scale * 100))) : "");
}

}  // namespace

const char* visualStateName(VisualState state) {
  switch (state) {
    case VisualState::Idle: return "idle";
    case VisualState::Hover: return "hover";
    case VisualState::Focus: return "focus";
    case VisualState::Pressed: return "pressed";
    case VisualState::Disabled: return "disabled";
  }
  return "?";
}

image::Image renderWidget(const BuildFn& build, const RenderSpec& spec, const VisualPaths& paths) {
  if (spec.width <= 0 || spec.height <= 0) throw std::invalid_argument("renderWidget: zero size");
  Rig& r = rig(paths);
  r.services.theme().set(spec.theme);
  UiContext ui(r.services);
  ui.setAnimationsEnabled(false);
  const auto physW = static_cast<uint32_t>(std::lround(spec.width * static_cast<double>(spec.scale)));
  const auto physH = static_cast<uint32_t>(std::lround(spec.height * static_cast<double>(spec.scale)));
  ui.setViewport(static_cast<int>(physW), static_cast<int>(physH), spec.scale);
  ui.setTime(1000);
  core::layout::Style& root = ui.rootStyle();
  root.direction = core::layout::FlexDirection::Column;
  root.alignItems = core::layout::Align::Start;
  for (double& p : root.padding) p = spec.padding;

  const core::tree::WidgetId target = build(ui, ui.root());
  ui.frame();
  const core::layout::Rect box = ui.absRect(target);
  const double cx = box.x + box.w / 2.0;
  const double cy = box.y + box.h / 2.0;
  switch (spec.state) {
    case VisualState::Idle: break;
    case VisualState::Hover: ui.pointerMove(cx, cy); break;
    case VisualState::Pressed:
      ui.pointerMove(cx, cy);
      ui.pointerDown(cx, cy);
      break;
    case VisualState::Focus: ui.router().focus(target, core::events::FocusReason::Keyboard); break;
    case VisualState::Disabled:
      if (WidgetObject* o = ui.object(target)) o->setEnabled(false);
      break;
  }

  render::OffscreenTarget offscreen(r.device, physW, physH);
  if (!renderFrame(ui, offscreen, clearColor(r.services, spec.background))) throw std::runtime_error("renderWidget: the offscreen frame was skipped");
  return toImage(offscreen.readPixels(), physW, physH);
}

std::string VisualResult::summary() const {
  if (!error.empty()) return error;
  return image::describe(metrics);
}

VisualResult compareWithReference(const BuildFn& build, const VisualSpec& spec, const VisualPaths& paths) {
  VisualResult result;
  const char* themeDir = spec.theme == theme::ThemeId::Light ? "light" : "dark";
  const std::filesystem::path referencePath = paths.referenceDir / "openpencil" / themeDir / (spec.reference + ".png");
  const image::DecodeResult reference = image::loadPng(referencePath);
  if (!reference.ok()) {
    result.error = reference.error;
    return result;
  }
  std::string error;
  const auto tolerance = image::loadTolerance(image::toleranceFilePath(paths.referenceDir), spec.profile, error);
  if (!tolerance) {
    result.error = error;
    return result;
  }
  RenderSpec render;
  render.width = static_cast<int>(std::lround(reference.image->width / static_cast<double>(spec.scale)));
  render.height = static_cast<int>(std::lround(reference.image->height / static_cast<double>(spec.scale)));
  render.theme = spec.theme;
  render.state = spec.state;
  render.scale = spec.scale;
  render.padding = spec.padding;
  render.background = spec.background;
  image::Image candidate = renderWidget(build, render, paths);

  // Ignored areas take the candidate's pixels in a copy of the reference, so they never fail.
  image::Image ref = *reference.image;
  if (ref.width == candidate.width && ref.height == candidate.height) {
    for (const VisualSpec::Ignore& ig : spec.ignore) {
      for (int y = std::max(0, ig.y); y < std::min<int>(ig.y + ig.h, static_cast<int>(ref.height)); ++y) {
        for (int x = std::max(0, ig.x); x < std::min<int>(ig.x + ig.w, static_cast<int>(ref.width)); ++x) {
          const size_t i = (size_t{static_cast<uint32_t>(y)} * ref.width + static_cast<uint32_t>(x)) * 4;
          std::copy_n(candidate.rgba.begin() + static_cast<std::ptrdiff_t>(i), 4, ref.rgba.begin() + static_cast<std::ptrdiff_t>(i));
        }
      }
    }
  }
  if (spec.luminance) {
    const auto gray = [](image::Image& img) {
      for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        const double y = 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] + 0.0722 * img.rgba[i + 2];
        img.rgba[i] = img.rgba[i + 1] = img.rgba[i + 2] = static_cast<uint8_t>(std::lround(y));
      }
    };
    image::Image grayCandidate = candidate;
    gray(ref);
    gray(grayCandidate);
    result.metrics = image::compare(ref, grayCandidate, *tolerance);
  } else {
    result.metrics = image::compare(ref, candidate, *tolerance);
  }
  result.renderPath = paths.artifactDir / (artifactName(spec) + ".png");
  image::writePng(result.renderPath, candidate.width, candidate.height, candidate.rgba);
  if (result.metrics.error.empty() && result.metrics.failingPixels != 0) {
    result.diffPath = paths.artifactDir / (artifactName(spec) + ".diff.png");
    const image::Image diff = image::makeDiffImage(candidate, result.metrics);
    image::writePng(result.diffPath, diff.width, diff.height, diff.rgba);
  }
  return result;
}

Services& sharedServices(const VisualPaths& paths) { return rig(paths).services; }

unsigned validationMessageCount() { return created() != nullptr ? created()->device.validationMessageCount() : 0u; }

double inkSum(const image::Image& image, int x, int y, int w, int h, const theme::Color& bg, const theme::Color& fg) {
  const double dr = static_cast<double>(fg.r) - bg.r;
  const double dg = static_cast<double>(fg.g) - bg.g;
  const double db = static_cast<double>(fg.b) - bg.b;
  const double denominator = dr * dr + dg * dg + db * db;
  if (denominator <= 0.0) return 0.0;
  double sum = 0.0;
  for (int py = std::max(0, y); py < std::min<int>(y + h, static_cast<int>(image.height)); ++py) {
    for (int px = std::max(0, x); px < std::min<int>(x + w, static_cast<int>(image.width)); ++px) {
      const uint8_t* p = image.rgba.data() + (size_t{static_cast<uint32_t>(py)} * image.width + static_cast<uint32_t>(px)) * 4;
      const double t = ((p[0] - bg.r) * dr + (p[1] - bg.g) * dg + (p[2] - bg.b) * db) / denominator;
      sum += std::clamp(t, 0.0, 1.0);
    }
  }
  return sum;
}

}  // namespace r1ui::widgets::testing
