// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the process-wide services shared by every window's UiContext: the active Theme over the
//   tokens, the StyleSheet (builtin rows plus rows registered by widget types), a cache of resolved
//   styles, the TextEngine and the IconCache.
// Why: multi-window readiness. Floating panels are separate OS windows with their own UiContext,
//   but they must share one theme (a switch repaints all of them), one glyph atlas, one icon atlas
//   and one style table instead of duplicating them.
// Callers: UiContext (one reference each), widgets through PaintContext / UiContext accessors, the
//   application shell (constructs it once), tests (NullTextureFactory).
// Style rows: a widget type exposes `static std::span<const theme::StyleRuleEntry> styleRows()`;
//   UiContext::create<T> hands it to addStyleRows once per distinct table (keyed by the address of
//   the first row), so adding a widget never edits a shared table. The new table must be valid
//   against the tokens or the whole addition is rejected (std::runtime_error naming the bad rows)
//   and the previous sheet stays in force.
// Resolution: resolve(key, state) returns the ResolvedStyle BY VALUE (about 150 bytes of plain data),
//   backed by a cache that is cleared when the theme revision or the sheet changes. A result stays
//   valid however long it is held: registering a new widget type's rows (the first create<NewType>) or
//   switching the theme while another window paints can not invalidate it. Binding it to a
//   `const ResolvedStyle&` extends the lifetime as before. An unknown key throws std::logic_error (a
//   programming error, found by the widget's tests).
// Threading: UI thread only.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "r1ui/render/Painter.h"
#include "r1ui/theme/StyleSheet.h"
#include "r1ui/theme/Theme.h"
#include "r1ui/widgets/icons/IconCache.h"
#include "r1ui/widgets/text/TextEngine.h"
#include "r1ui/widgets/text/TextureFactory.h"

namespace r1ui::widgets {

struct ServicesPaths {
  std::filesystem::path fontDir;                   // contains Inter-Regular.ttf
  std::vector<std::filesystem::path> iconDirs;     // searched in order (Lucide first)
};

class Services {
 public:
  // Throws std::runtime_error when the font is missing or the builtin style table is rejected by
  // the tokens. `textures` must outlive the services.
  Services(std::shared_ptr<const theme::Tokens> tokens, TextureFactory& textures, const ServicesPaths& paths);
  Services(const Services&) = delete;
  Services& operator=(const Services&) = delete;

  theme::Theme& theme() { return theme_; }
  const theme::Theme& theme() const { return theme_; }
  const theme::Tokens& tokens() const { return theme_.tokens(); }
  TextEngine& text() { return text_; }
  IconCache& icons() { return icons_; }

  // ---- styles ----
  // Adds rows once per distinct table identity (the address of rows.data()); no-op when already
  // added or when `rows` is empty. Throws std::runtime_error (sheet unchanged) when rejected.
  void addStyleRows(std::span<const theme::StyleRuleEntry> rows);
  // Bumped whenever rows were added; contexts repaint and caches reset.
  uint32_t sheetRevision() const { return sheetRevision_; }
  bool hasStyleKey(std::string_view key) const { return sheet_->hasKey(key); }
  theme::ResolvedStyle resolve(std::string_view key, uint8_t state);

  // ---- colours ----
  // Theme colour by token name as a painter colour; opaque magenta for an unknown name so a
  // missing token is visible instead of invisible.
  render::Color color(std::string_view token, double opacity = 1.0) const;
  static render::Color toRender(const theme::Color& c, double opacity = 1.0);

 private:
  struct KeyHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    size_t operator()(const std::string& s) const { return std::hash<std::string_view>{}(s); }
  };
  struct CachedState {
    uint8_t state = 0;
    theme::ResolvedStyle value;
  };

  theme::Theme theme_;
  std::vector<theme::StyleRuleEntry> rules_;
  std::vector<const void*> addedTables_;
  std::unique_ptr<theme::StyleSheet> sheet_;
  uint32_t sheetRevision_ = 1;
  std::unordered_map<std::string, std::vector<CachedState>, KeyHash, std::equal_to<>> cache_;
  uint32_t cacheThemeRevision_ = 0;
  uint32_t cacheSheetRevision_ = 0;
  TextEngine text_;
  IconCache icons_;
};

}  // namespace r1ui::widgets
