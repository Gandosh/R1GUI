// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: oracle for the preview scene (tree + flex layout + router + invalidator + style sheet):
//   the panel geometry equals the rectangles measured on the reference screenshot, relayout after
//   resizing (including degenerate sizes) restores the same result, hover requests exactly the frames
//   it needs and none when idle, the Name field edits through router events, and hostile input
//   (NaN/huge coordinates, invalid code points, random keys) never breaks the scene.
// Why: these behaviours span five modules that were built separately; this is where they first meet.
// Callers: CTest (label gpu: the text engine needs a Vulkan device, no window is created).
// Reference numbers (panel top at screen y 41, window title bar 32 px): see PanelBuild.cpp.
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <random>

#include "Scene.h"
#include "r1ui/render/RenderDevice.h"

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
  preview::TextEngine text(textures, assets + "/fonts");
  preview::IconSet icons(textures, {assets + "/icons/lucide", assets + "/icons/custom"});
  std::string clipboard;
  preview::SceneHost host;
  host.writeClipboard = [&](std::string_view s) { clipboard = std::string(s); };
  host.readClipboard = [&]() -> std::optional<std::string> { return clipboard; };
  preview::Scene scene(tokens, text, icons, host);
  scene.setViewport(1440, 900, 1.0f);
  scene.layout();
  scene.prepareIcons();

  // ---- Geometry against the reference screenshot ---------------------------------------------
  const auto& tree = scene.tree();
  const auto content = tree.firstChild(scene.ids().panel);
  expectRect(scene.absRect(scene.ids().titleBar), 0, 0, 1440, 32, "title bar");
  expectRect(scene.absRect(scene.ids().panel), 0, 32, 258, 868, "panel column");
  expectRect(scene.absRect(content), 0, 32, 258, 723, "panel content is 723 px tall like the reference");
  // Section tops measured on the reference (local y + 32): strip 0, header 40, Position 83, Layout 206,
  // Appearance 266, Fill 450, Stroke 576, Effects 625, Export 674.
  const int expectedTops[] = {0, 40, 83, 206, 266, 450, 576, 625, 674};
  auto child = tree.firstChild(content);
  for (int top : expectedTops) {
    expect(child.valid(), "section exists");
    expect(scene.absRect(child).y == 32 + top, "section top equals the reference");
    expect(scene.absRect(child).w == 258 || scene.absRect(child).w == 234 || scene.absRect(child).w == 258, "section spans the panel");
    child = tree.nextSibling(child);
  }
  expect(!child.valid(), "nine blocks in the panel");
  // Position: X/Y row is the third child of the section; two 114 px fields with a 6 px gap.
  auto position = tree.nextSibling(tree.nextSibling(tree.firstChild(content)));
  auto row = tree.nextSibling(tree.nextSibling(tree.firstChild(position)));  // title, align, then X/Y
  expectRect(scene.absRect(row), 12, 32 + 140, 234, 26, "X/Y row");
  expectRect(scene.absRect(tree.firstChild(row)), 12, 32 + 140, 114, 26, "X field 114x26");
  expectRect(scene.absRect(tree.nextSibling(tree.firstChild(row))), 132, 32 + 140, 114, 26, "Y field starts after the 6 px gap");

  // ---- Resize and degenerate viewports -------------------------------------------------------
  for (const auto size : {std::pair{0, 0}, std::pair{1, 1}, std::pair{50, 30}, std::pair{258, 100}, std::pair{4000, 3000}}) {
    scene.setViewport(size.first, size.second, 1.0f);
    scene.layout();
  }
  scene.setViewport(1440, 900, 1.0f);
  scene.layout();
  expectRect(scene.absRect(scene.ids().panel), 0, 32, 258, 868, "panel after the resize round trip");
  scene.setViewport(1440, 900, 1.5f);
  scene.layout();
  expectRect(scene.absRect(scene.ids().panel), 0, 32, 258, 568, "panel at 150%: logical size follows the scale");
  scene.setViewport(1440, 900, 1.0f);
  scene.layout();

  // ---- Hover, idle and damage ---------------------------------------------------------------
  expect(!scene.needsFrame(), "no frame is wanted when nothing changed");
  scene.handleEvent(mouse(EventType::MouseMove, 60, 185), 10);
  expect(scene.needsFrame(), "entering a field requests a frame");
  expect(scene.cursor() == r1ui::platform::CursorShape::ResizeHorizontal, "number fields show the resize cursor");
  scene.layout();
  expect(!scene.needsFrame(), "the frame was consumed");
  scene.handleEvent(mouse(EventType::MouseMove, 700, 500), 20);
  expect(scene.needsFrame() && scene.cursor() == r1ui::platform::CursorShape::Arrow, "leaving restores the arrow and requests a frame");
  scene.layout();
  scene.handleEvent(mouse(EventType::MouseMove, 701, 501), 30);
  expect(!scene.needsFrame(), "moving over the canvas requests nothing");

  // ---- Name field editing through the router ------------------------------------------------
  const auto nameRect = scene.absRect(scene.ids().nameField);
  const float nx = static_cast<float>(nameRect.x + nameRect.w - 8);
  const float ny = static_cast<float>(nameRect.y + nameRect.h / 2);
  scene.handleEvent(mouse(EventType::MouseDown, nx, ny), 100);
  scene.handleEvent(mouse(EventType::MouseUp, nx, ny), 110);
  expect(scene.textFieldFocused(), "clicking the Name field focuses it");
  expect(scene.cursor() == r1ui::platform::CursorShape::Text, "the text cursor shows over the Name field");
  const std::string before = scene.nameValue();
  Event ch;
  ch.type = EventType::Char;
  ch.codePoint = U'é';
  scene.handleEvent(ch, 120);
  expect(scene.nameValue() == before + "\xC3\xA9", "typing inserts UTF-8");
  Event back;
  back.type = EventType::KeyDown;
  back.virtualKey = 8;
  scene.handleEvent(back, 130);
  expect(scene.nameValue() == before, "Backspace removes the whole character");
  Event key;
  key.type = EventType::KeyDown;
  key.virtualKey = 'A';
  key.modifiers.ctrl = true;
  scene.handleEvent(key, 140);
  key.virtualKey = 'C';
  scene.handleEvent(key, 150);
  expect(clipboard == before, "Ctrl+A, Ctrl+C copy the whole text to the clipboard hook");
  expect(scene.msUntilTick(160).has_value(), "a focused field schedules the caret blink");
  scene.layout();
  expect(scene.tick(130 + 600) && scene.needsFrame(), "the blink tick requests a frame");
  Event escape;
  escape.type = EventType::KeyDown;
  escape.virtualKey = 27;
  scene.handleEvent(escape, 800);
  expect(!scene.textFieldFocused() && !scene.msUntilTick(900).has_value(), "Escape leaves the field and stops the blink");

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
  expect(scene.widgetCount() > 100, "the tree survived the input storm");

  if (failures == 0) std::printf("scene_test: ok\n");
  return failures == 0 ? 0 : 1;
}
