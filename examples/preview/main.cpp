// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the R1GUI interactive preview entry point. Phase 1 content: a viewer with two modes,
//   (1) design-token colour swatches for the dark/light theme and (2) the reference screenshots.
//   Tab switches mode, T toggles dark/light, Left/Right (or clicking the window halves) step
//   through screenshots, Esc or the close button exits.
// Why: owner requirement that every phase ends with something launchable to interact with; built
//   from the real modules (ui-theme tokens, ui-platform window and input, ui-render drawing).
// Callers: the OS. The renderer frees the displayed image when it is destroyed. Calls: r1ui::theme::Tokens, r1ui::platform::Window, r1ui::render::Renderer.
// Assets: resolved next to the executable (assets/theme/tokens.json and reference/<theme>/
//   screen-*.r1img, both put there by the build). Missing or damaged assets end the program with
//   a message box naming the problem, never a crash.
#include <windows.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "RawImage.h"
#include "r1ui/core/CheckedCast.h"
#include "r1ui/platform/Window.h"
#include "r1ui/render/Renderer.h"
#include "r1ui/theme/Tokens.h"

namespace {

namespace fs = std::filesystem;
using r1ui::platform::Window;
using r1ui::theme::Color;
using r1ui::theme::ThemeId;

// ---- Swatch grid geometry (physical pixels) ---------------------------------------------
constexpr int kColumns = 8;
constexpr int kSwatch = 48;
constexpr int kGap = 8;
constexpr int kMargin = 24;

enum class Mode { Swatches, Screens };

fs::path executableDir() {
  std::wstring buffer(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), r1ui::core::checkedCast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) throw std::runtime_error("cannot locate the executable");
  buffer.resize(length);
  return fs::path(buffer).parent_path();
}

// Lists reference screens for one theme, sorted by file name; names are never hard-coded.
std::vector<fs::path> listScreens(const fs::path& referenceDir, ThemeId theme) {
  std::vector<fs::path> screens;
  std::error_code error;
  for (const auto& entry : fs::directory_iterator(referenceDir / r1ui::theme::themeName(theme), error)) {
    const fs::path& path = entry.path();
    if (path.extension() == ".r1img" && path.filename().string().rfind("screen-", 0) == 0) {
      screens.push_back(path);
    }
  }
  std::sort(screens.begin(), screens.end());
  return screens;
}

// Composites a straight-alpha colour over an opaque background (the blit path ignores alpha).
r1ui::render::Rgba8 flatten(const Color& color, const Color& background) {
  const auto mix = [&](uint8_t fg, uint8_t bg) {
    return static_cast<uint8_t>((fg * color.a + bg * (255 - color.a) + 127) / 255);
  };
  return {mix(color.r, background.r), mix(color.g, background.g), mix(color.b, background.b), 255};
}

class Viewer {
 public:
  Viewer(Window& window, r1ui::render::Renderer& renderer, r1ui::theme::Tokens tokens,
         const fs::path& referenceDir)
      : window_(window), renderer_(renderer), tokens_(std::move(tokens)) {
    for (ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
      screens_[static_cast<size_t>(theme)] = listScreens(referenceDir, theme);
      if (screens_[static_cast<size_t>(theme)].empty()) {
        throw std::runtime_error("no reference screens found for the " +
                                 std::string(r1ui::theme::themeName(theme)) + " theme in " +
                                 (referenceDir / r1ui::theme::themeName(theme)).string());
      }
    }
  }

  Viewer(const Viewer&) = delete;
  Viewer& operator=(const Viewer&) = delete;

  // One iteration: apply input, then draw the current mode.
  void frame() {
    handleInput();
    if (mode_ == Mode::Screens) syncImage();
    r1ui::render::FrameContent content;
    const Color canvas = *tokens_.color(theme_, "canvas");
    content.clear = {canvas.r / 255.0f, canvas.g / 255.0f, canvas.b / 255.0f};
    if (mode_ == Mode::Swatches) {
      drawSwatches(content, canvas);
    } else {
      content.image = image_;
    }
    updateTitle();
    renderer_.drawFrame(window_, content);
  }

 private:
  const std::vector<fs::path>& screens() const { return screens_[static_cast<size_t>(theme_)]; }

  void handleInput() {
    namespace keys = r1ui::platform::keys;
    for (const auto& event : window_.takeKeyEvents()) {
      switch (event.virtualKey) {
        case keys::kTab: mode_ = mode_ == Mode::Swatches ? Mode::Screens : Mode::Swatches; break;
        case keys::kT: theme_ = theme_ == ThemeId::Dark ? ThemeId::Light : ThemeId::Dark; break;
        case keys::kLeft: if (mode_ == Mode::Screens) step(-1); break;
        case keys::kRight: if (mode_ == Mode::Screens) step(+1); break;
        default: break;
      }
    }
    for (const auto& click : window_.takeMouseClicks()) {
      if (mode_ == Mode::Screens) step(click.x < static_cast<float>(window_.clientWidth()) / 2 ? -1 : +1);
    }
  }

  void step(int delta) {
    const int count = r1ui::core::checkedCast<int>(screens().size());
    index_ = (r1ui::core::checkedCast<int>(index_) + delta + count) % count;
  }

  // Keeps the uploaded image in step with the selected theme and index; one image lives at a time.
  void syncImage() {
    index_ = std::min(index_, screens().size() - 1);  // theme switch may shrink the list
    if (image_.valid() && loadedTheme_ == theme_ && loadedIndex_ == index_) return;
    const fs::path& path = screens()[index_];
    RawImageResult raw = loadRawImage(path);
    if (!raw.image) throw std::runtime_error(raw.error);
    const r1ui::render::ImageId fresh = renderer_.uploadImage(raw.image->width, raw.image->height, raw.image->bgra.data());
    if (image_.valid()) renderer_.destroyImage(image_);
    image_ = fresh;
    loadedTheme_ = theme_;
    loadedIndex_ = index_;
  }

  static int cellOrigin(int cell) { return kMargin + cell * (kSwatch + kGap); }

  // Index of the swatch under the pointer, or nullopt over a gap, margin or past the last colour.
  std::optional<size_t> swatchAt(float px, float py) const {
    const int x = static_cast<int>(px) - kMargin;
    const int y = static_cast<int>(py) - kMargin;
    if (x < 0 || y < 0) return std::nullopt;
    const int col = x / (kSwatch + kGap);
    const int row = y / (kSwatch + kGap);
    if (col >= kColumns || x % (kSwatch + kGap) >= kSwatch || y % (kSwatch + kGap) >= kSwatch) return std::nullopt;
    const size_t index = static_cast<size_t>(row) * kColumns + static_cast<size_t>(col);
    if (index >= tokens_.colorCount()) return std::nullopt;
    return index;
  }

  void drawSwatches(r1ui::render::FrameContent& content, const Color& canvas) {
    const std::optional<size_t> hover = swatchAt(window_.mouseX(), window_.mouseY());
    const Color accent = *tokens_.color(theme_, "accent");
    for (size_t i = 0; i < tokens_.colorCount(); ++i) {
      const int x = cellOrigin(static_cast<int>(i % kColumns));
      const int y = cellOrigin(static_cast<int>(i / kColumns));
      const bool hovered = hover && *hover == i;
      // Outline behind the swatch keeps colours equal to the canvas visible; hover thickens it.
      const int outline = hovered ? 3 : 1;
      const r1ui::render::Rgba8 outlineColor = hovered ? flatten(accent, canvas) : r1ui::render::Rgba8{128, 128, 128, 255};
      content.rects.push_back({x - outline, y - outline, kSwatch + 2 * outline, kSwatch + 2 * outline, outlineColor});
      content.rects.push_back({x, y, kSwatch, kSwatch, flatten(*tokens_.colorAt(theme_, i), canvas)});
    }
    hoverIndex_ = hover;
  }

  void updateTitle() {
    std::string title;
    const char* theme = r1ui::theme::themeName(theme_);
    if (mode_ == Mode::Swatches) {
      if (hoverIndex_) {
        title = std::string(theme) + ": " + tokens_.colorNames()[*hoverIndex_] + " " +
                r1ui::theme::toHex(*tokens_.colorAt(theme_, *hoverIndex_));
      } else {
        title = std::string("R1GUI Preview - tokens (") + theme + ")  Tab: screens, T: theme";
      }
    } else {
      title = std::string(theme) + " " + screens()[index_].stem().string() + " (" +
              std::to_string(index_ + 1) + "/" + std::to_string(screens().size()) + ")";
    }
    if (title != title_) {
      window_.setTitle(title);
      title_ = std::move(title);
    }
  }

  Window& window_;
  r1ui::render::Renderer& renderer_;
  r1ui::theme::Tokens tokens_;
  std::vector<fs::path> screens_[2];
  Mode mode_ = Mode::Swatches;
  ThemeId theme_ = ThemeId::Dark;
  size_t index_ = 0;
  r1ui::render::ImageId image_;
  ThemeId loadedTheme_ = ThemeId::Dark;
  size_t loadedIndex_ = 0;
  std::optional<size_t> hoverIndex_;
  std::string title_;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  try {
    const fs::path exeDir = executableDir();
    auto tokens = r1ui::theme::Tokens::loadFile(exeDir / "assets" / "theme" / "tokens.json");
    if (!tokens.ok()) {
      throw std::runtime_error("Design tokens are missing or invalid:\n" + tokens.error +
                               "\n\nRebuild the r1gui-preview target to copy assets next to the executable.");
    }
    Window window({.title = "R1GUI Preview - Phase 1", .width = 1280, .height = 720});
    r1ui::render::Renderer renderer(window);
    Viewer viewer(window, renderer, std::move(*tokens.tokens), exeDir / "reference");

    while (window.pumpEvents() && !window.escapePressed()) {
      window.consumeResized();  // the renderer compares sizes itself; this just clears the flag
      viewer.frame();
      if (window.clientWidth() == 0) Sleep(16);  // minimized: avoid spinning
    }
    return 0;
  } catch (const std::exception& e) {
    MessageBoxA(nullptr, e.what(), "R1GUI Preview error", MB_OK | MB_ICONERROR);
    return 1;
  }
}
