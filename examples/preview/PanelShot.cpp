// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of PanelShot.h.
// Callers: main.cpp.
#include "PanelShot.h"

#include <stdexcept>
#include <string>
#include <vector>

#include "Assets.h"
#include "Toolkit.h"
#include "Scene.h"
#include "r1ui/widgets/image/Png.h"
#include "r1ui/render/OffscreenTarget.h"
#include "r1ui/render/RenderDevice.h"

namespace preview {

void renderPanelShot(const std::filesystem::path& directory, r1ui::theme::ThemeId theme) {
  const AssetPaths paths{executableDir()};
  auto tokens = std::make_shared<const r1ui::theme::Tokens>(loadTokens(paths));
  r1ui::render::RenderDevice device;
  r1ui::render::OffscreenTarget target(device, kShotWidth, kShotHeight);
  GpuTextureFactory textures(device);
  TextEngine text(textures, paths.fonts());
  IconSet icons(textures, {paths.icons(), paths.customIcons()});
  Scene scene(tokens, text, icons, {});
  if (theme != scene.theme().id()) scene.theme().set(theme);
  scene.setViewport(static_cast<int>(kShotWidth), static_cast<int>(kShotHeight), 1.0f);
  scene.layout();
  scene.prepareIcons();

  const auto canvas = scene.theme().color("canvas");
  if (!target.beginFrame(r1ui::render::Color::fromRgba8(canvas->r, canvas->g, canvas->b))) throw std::runtime_error("the offscreen target cannot draw");
  scene.paint(target.painter());
  text.uploadAtlas();
  if (!target.endFrame()) throw std::runtime_error("the offscreen frame was skipped");
  const std::vector<uint8_t> pixels = target.readPixels();

  const std::string name = r1ui::theme::themeName(scene.theme().id());
  std::filesystem::create_directories(directory);
  r1ui::widgets::image::writePng(directory / ("scene_" + name + ".png"), kShotWidth, kShotHeight, pixels);

  const auto panel = scene.panelRect();
  std::vector<uint8_t> crop;
  crop.reserve(size_t{kPanelCropWidth} * kPanelCropHeight * 4);
  for (uint32_t y = 0; y < kPanelCropHeight; ++y) {
    const size_t from = ((size_t{static_cast<uint32_t>(panel.y)} + y) * kShotWidth + static_cast<uint32_t>(panel.x)) * 4;
    crop.insert(crop.end(), pixels.begin() + static_cast<std::ptrdiff_t>(from),
                pixels.begin() + static_cast<std::ptrdiff_t>(from + size_t{kPanelCropWidth} * 4));
  }
  r1ui::widgets::image::writePng(directory / ("panel_" + name + ".png"), kPanelCropWidth, kPanelCropHeight, crop);
}

}  // namespace preview
