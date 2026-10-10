// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of Shots.h.
// Invariants: one device, one services object (hence one glyph atlas and icon cache) per call; a
//   screen is laid out, painted once and read back; animations are instant (no frame loop declared).
// Callers: main.cpp.
#include "Shots.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Assets.h"
#include "ComposedApp.h"
#include "ComposedUtil.h"
#include "GalleryApp.h"
#include "editor/EditorApp.h"
#include "Scene.h"
#include "Toolkit.h"
#include "r1ui/render/OffscreenTarget.h"
#include "r1ui/render/RenderDevice.h"
#include "r1ui/widgets/dock/InWindowFloatingBackend.h"
#include "r1ui/widgets/image/Png.h"
#include "r1ui/widgets/runtime/Services.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/thumbnailgrid/GpuThumbnailTextures.h"

namespace preview {

namespace widgets = r1ui::widgets;

namespace {

struct Renderer {
  Renderer(const AssetPaths& paths, r1ui::theme::ThemeId theme)
      : tokens(std::make_shared<const r1ui::theme::Tokens>(loadTokens(paths))),
        device(),
        target(device, kShotWidth, kShotHeight),
        textures(device),
        services(tokens, textures, widgets::ServicesPaths{paths.fonts(), {paths.icons(), paths.customIcons()}}),
        scene(services.theme(), services.text()) {
    services.theme().set(theme);
    scene.setViewport(static_cast<int>(kShotWidth), static_cast<int>(kShotHeight), 1.0f);
    scene.layout();
  }

  void setMode(Mode mode) {
    scene.setMode(mode);
    scene.layout();
  }

  // A context with the title bar's height reserved; `content` receives the container to fill.
  std::unique_ptr<widgets::UiContext> context(r1ui::core::tree::WidgetId& content) {
    auto ui = std::make_unique<widgets::UiContext>(services);
    ui->rootStyle().direction = r1ui::core::layout::FlexDirection::Column;
    ui->rootStyle().padding[r1ui::core::layout::kTop] = kTitleBarHeight;
    widgets::SectionBox& area = build::flex(*ui, ui->root(), true);
    build::grow(area.style());
    content = area.id();
    ui->setViewport(static_cast<int>(kShotWidth), static_cast<int>(kShotHeight), 1.0f);
    return ui;
  }

  void write(widgets::UiContext& ui, const std::filesystem::path& file) {
    ui.frame();
    const auto canvas = services.theme().color("canvas");
    for (int pass = 0; pass < 2; ++pass) {  // a second pass when an atlas ran out of room
      if (!target.beginFrame(r1ui::render::Color::fromRgba8(canvas->r, canvas->g, canvas->b))) throw std::runtime_error("the offscreen target cannot draw");
      services.text().beginFrame();
      scene.paint(target.painter());
      ui.paint(target.painter());
      ui.finishPaint();
      if (!target.endFrame()) throw std::runtime_error("the offscreen frame was skipped");
      if (!ui.consumeRepaint()) break;
    }
    widgets::image::writePng(file, kShotWidth, kShotHeight, target.readPixels());
  }

  std::shared_ptr<const r1ui::theme::Tokens> tokens;
  r1ui::render::RenderDevice device;
  r1ui::render::OffscreenTarget target;
  GpuTextureFactory textures;
  widgets::Services services;
  Scene scene;
};

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

}  // namespace

void renderShots(const std::filesystem::path& directory, r1ui::theme::ThemeId theme) {
  const AssetPaths paths{executableDir()};
  Renderer r(paths, theme);
  const std::string name = r1ui::theme::themeName(theme);
  std::filesystem::create_directories(directory);

  r1ui::core::tree::WidgetId content;
  {
    r.setMode(Mode::Widgets);
    auto ui = r.context(content);
    ComposedHost host;
    host.setDarkTheme = [&](bool dark) { r.services.theme().set(dark ? r1ui::theme::ThemeId::Dark : r1ui::theme::ThemeId::Light); };
    host.isDark = [&] { return r.services.theme().id() == r1ui::theme::ThemeId::Dark; };
    host.quit = [] {};
    ComposedApp app(*ui, content, std::move(host));
    r.write(*ui, directory / ("widgets_" + name + ".png"));
  }
  r.setMode(Mode::Gallery);
  for (size_t page = 0; page < GalleryApp::kPageCount; ++page) {
    auto ui = r.context(content);
    GalleryApp gallery(*ui, content, page);
    r.write(*ui, directory / ("gallery_" + lower(GalleryApp::pageName(page)) + "_" + name + ".png"));
  }

  // The Editor screen over the in-window backend (a native window cannot be rendered offscreen) with its
  // own throwaway data folder: the arrangement, the hotkey editor, the menu creator, the Custom Menus menu
  // content (the sample Quick Tools panel) and a floating panel.
  r.setMode(Mode::Editor);
  {
    auto ui = r.context(content);
    r1ui::widgets::InWindowFloatingBackend backend(*ui, ui->root());
    editor::EditorHost host;
    host.dataRoot = directory / ("editor-data-" + name);
    std::error_code ignored;
    std::filesystem::remove_all(host.dataRoot, ignored);
    host.setDarkTheme = [&](bool dark) { r.services.theme().set(dark ? r1ui::theme::ThemeId::Dark : r1ui::theme::ThemeId::Light); };
    host.isDark = [&] { return r.services.theme().id() == r1ui::theme::ThemeId::Dark; };
    host.quit = [] {};
    widgets::GpuThumbnailTextures brushSink(r.device);  // outlives the app: its pictures are released first
    host.thumbnailSink = &brushSink;
    editor::EditorApp app(*ui, content, backend, std::move(host));
    const auto shot = [&](const char* tag) {
      app.update();
      r.write(*ui, directory / (std::string("editor_") + tag + name + ".png"));
    };
    // Dialogs and the creator open from timers: advance the context clock like the app loop does.
    const auto settle = [&] {
      for (int i = 0; i < 4; ++i) {
        ui->setTime(ui->now() + 60);
        ui->tick();
        app.update();
        ui->frame();
      }
    };
    settle();
    shot("");
    app.run("edit.shortcuts");
    settle();
    shot("shortcuts_");
    app.run("custommenu.create");
    settle();
    shot("creator_");
    app.dock().floatPanel(editor::panel::kCurves);
    settle();
    shot("floating_");
    // The brush library over the viewport: opened (pictures arrive in batches), then after typing S.
    app.brushLibrary().open();
    settle();
    shot("brushes_");
    shot("brushes_");
    ui->textInput(U's');
    settle();
    shot("brushes_s_");
    app.brushLibrary().close();
  }
}

}  // namespace preview
