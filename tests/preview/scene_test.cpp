// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for the preview shell's title-bar scene (tree + flex layout + router + invalidator +
//   style sheet over the shared theme): the title bar and body rectangles, the chrome regions the
//   platform hit-tests, relayout after resizing (including degenerate sizes), the mode indicator
//   (one square per mode, exactly one selected), hover requesting exactly the frames it needs and
//   none when idle, the theme switch through the shared theme, and hostile input (NaN/huge
//   coordinates, invalid code points, random keys) never breaking the scene.
// Why: these behaviours span five modules that were built separately; this is where they first meet.
// Callers: CTest (label gpu: the text engine needs a Vulkan device, no window is created).
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <random>

#include "Scene.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/widgets/runtime/Services.h"

namespace {

using r1ui::core::layout::Rect;
using r1ui::platform::Event;
using r1ui::platform::EventType;

int failures = 0;

void expect(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

void expectRect(const Rect& got, int x, int y, int w, int h, const char* what) {
  if (got.x != x || got.y != y || got.w != w || got.h != h) {
    std::fprintf(stderr, "FAIL: %s: got (%d,%d,%d,%d) expected (%d,%d,%d,%d)\n", what, got.x, got.y, got.w, got.h, x, y, w, h);
    ++failures;
  }
}

Event mouse(EventType type, float x, float y) {
  Event e;
  e.type = type;
  e.x = x;
  e.y = y;
  return e;
}

}  // namespace

int main() {
  const std::string assets = R1UI_ASSETS_DIR;
  auto parsed = r1ui::theme::Tokens::loadFile(assets + "/theme/tokens.json", {.requireAllSections = true});
  if (!parsed.ok()) {
    std::fprintf(stderr, "tokens: %s\n", parsed.error.c_str());
    return 2;
  }
  auto tokens = std::make_shared<const r1ui::theme::Tokens>(std::move(*parsed.tokens));
  r1ui::render::RenderDevice device;
  r1ui::widgets::GpuTextureFactory textures(device);
  r1ui::widgets::Services services(tokens, textures, {assets + "/fonts", {assets + "/icons/lucide", assets + "/icons/custom"}});
  preview::Scene scene(services.theme(), services.text());
  scene.setViewport(1440, 900, 1.0f);
  scene.layout();

  // ---- Geometry: the 32 px title bar, the body below it, the three caption buttons ---------------
  expectRect(scene.absRect(scene.ids().titleBar), 0, 0, 1440, 32, "title bar");
  expectRect(scene.absRect(scene.ids().body), 0, 32, 1440, 868, "body below the title bar");
  expectRect(scene.absRect(scene.ids().close), 1440 - 46, 0, 46, 32, "close button at the right edge");
  expectRect(scene.absRect(scene.ids().maximize), 1440 - 92, 0, 46, 32, "maximise button left of close");
  expectRect(scene.absRect(scene.ids().minimize), 1440 - 138, 0, 46, 32, "minimise button left of maximise");
  const r1ui::platform::ChromeLayout chrome = scene.chromeLayout();
  expect(chrome.closeButton.x == 1440 - 46 && chrome.closeButton.width == 46 && chrome.captionRects.size() == 1, "chrome layout follows the buttons");
  const auto body = scene.bodyRect();
  expect(body.x == 0.0f && body.y == 32.0f && body.w == 1440.0f && body.h == 868.0f, "bodyRect is physical pixels below the bar");

  // ---- Mode indicator: five squares, exactly the current mode selected ----------------------------
  expect(preview::kModeCount == 6, "six modes");
  for (int mode = 0; mode < preview::kModeCount; ++mode) {
    scene.setMode(static_cast<preview::Mode>(mode));
    int selected = 0;
    for (int i = 0; i < preview::kModeCount; ++i) selected += (scene.squareState(i) & r1ui::theme::State::kSelected) != 0 ? 1 : 0;
    expect(selected == 1 && (scene.squareState(mode) & r1ui::theme::State::kSelected) != 0, "exactly the current mode's square is selected");
  }
  scene.setMode(preview::Mode::Gallery);
  scene.layout();

  // ---- Resize and degenerate viewports -------------------------------------------------------
  for (const auto size : {std::pair{0, 0}, std::pair{1, 1}, std::pair{50, 30}, std::pair{258, 100}, std::pair{4000, 3000}}) {
    scene.setViewport(size.first, size.second, 1.0f);
    scene.layout();
  }
  scene.setViewport(1440, 900, 1.0f);
  scene.layout();
  expectRect(scene.absRect(scene.ids().body), 0, 32, 1440, 868, "body after the resize round trip");
  scene.setViewport(1440, 900, 1.5f);
  scene.layout();
  expectRect(scene.absRect(scene.ids().body), 0, 32, 960, 568, "body at 150%: logical size follows the scale");
  scene.setViewport(1440, 900, 1.0f);
  scene.layout();

  // ---- Hover, idle and damage ---------------------------------------------------------------
  expect(!scene.needsFrame(), "no frame is wanted when nothing changed");
  scene.handleEvent(mouse(EventType::MouseMove, 1440 - 20, 16), 10);
  expect(scene.needsFrame(), "entering the close button requests a frame");
  scene.layout();
  expect(!scene.needsFrame(), "the frame was consumed");
  scene.handleEvent(mouse(EventType::MouseMove, 700, 500), 20);
  expect(scene.needsFrame(), "leaving requests a frame");
  scene.layout();
  scene.handleEvent(mouse(EventType::MouseMove, 701, 501), 30);
  expect(!scene.needsFrame(), "moving over the body requests nothing");

  // ---- The theme is the shared one --------------------------------------------------------------
  const auto before = services.theme().id();
  scene.toggleTheme();
  expect(services.theme().id() != before && scene.theme().id() == services.theme().id(), "the scene toggles the shared theme");
  expect(scene.needsFrame(), "a theme switch requests a frame");
  scene.layout();
  scene.toggleTheme();
  scene.layout();

  // ---- Hostile input -------------------------------------------------------------------------
  std::mt19937 rng(7);
  const float specials[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1e30f, 1e30f, -5.0f, 0.0f};
  for (int i = 0; i < 20000; ++i) {
    Event e;
    e.type = static_cast<EventType>(rng() % 16);
    e.x = (rng() % 4 == 0) ? specials[rng() % 6] : static_cast<float>(rng() % 1500);
    e.y = (rng() % 4 == 0) ? specials[rng() % 6] : static_cast<float>(rng() % 950);
    e.wheelY = specials[rng() % 6];
    e.virtualKey = rng() % 300;
    const char32_t points[] = {0, 0xD800, 0xDFFF, 0x110000, 0x7FFFFFFF, U'a', U'中', 0x1F600};
    e.codePoint = points[rng() % 8];
    e.modifiers.ctrl = (rng() & 1) != 0;
    e.modifiers.shift = (rng() & 1) != 0;
    e.rect = {0, 0, static_cast<int>(rng() % 2000), static_cast<int>(rng() % 1200)};
    scene.handleEvent(e, static_cast<uint64_t>(i));
    if (i % 200 == 0) scene.layout();
  }
  scene.layout();
  expect(scene.widgetCount() > 10, "the tree survived the input storm");

  if (failures == 0) std::printf("scene_test: ok\n");
  return failures == 0 ? 0 : 1;
}
