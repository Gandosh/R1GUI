// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the visual of tooltips on top of the foundation's TooltipManager: TooltipContent (the measured
//   tooltip box content: one or more title lines in 12 px `surface` text, an optional shortcut text
//   after the title, an optional wrapped description in muted 11 px), the RichTooltips registry that
//   attaches rich content to a widget, and the measured timing (410 ms to appear).
// Why: the manager owns when a tooltip appears and where; what is inside is a style decision that
//   the reference fixes (box 26 px high for one line: 12 px text with a 16 px line, 4 px padding and a
//   1 px border; radius 6; text `surface`, background `panel`, border `border`, shadow lg), plus the
//   rich variants of spec 10 rule 10 to 12 (description followed by the shortcut in brackets, a
//   custom-content tooltip with its own text roles, wrapping at 1000 px).
// Callers: applications call RichTooltips::install(ui) once per UiContext, then setTooltip() on
//   widgets as before, or RichTooltips::set() for a title with shortcut and description. Calls:
//   TooltipManager (content factory and timing), OverlayHost (the surface; the border has no layout
//   effect, so the content keeps a 1 px margin).
// Plain tooltips: with the factory installed every tooltip, including plain `setTooltip(text)` ones,
//   gets the measured box (the foundation's default label is 2 px too short because of the border).
//   The text is the source widget's tooltip text; a shortcut given as `(P)` is part of that text, as
//   in the reference ("Pen (P)"); tooltipWithShortcut() in MenuModel.h builds it.
// Timing: kMeasuredTooltipTiming is 50 ms rest + 360 ms = 410 ms before the tooltip appears (the
//   measured open delay of the reference, 412 ms dark and 416 ms light; spec 10's 0.2 s is the
//   toolkit's own earlier default), then a 100 ms fade. Applications may call
//   ui.tooltips().setTiming() afterwards to tune it.
// Boundaries: all texts are sanitised (invalid UTF-8 replaced, 4096 byte limit); lines wrap at
//   1000 px (titles) and 280 px (descriptions), at most 12 lines per block.
#pragma once

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/overlay/TooltipManager.h"
#include "r1ui/widgets/runtime/PaintContext.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

inline constexpr TooltipTiming kMeasuredTooltipTiming{50, 360, 100};
inline constexpr double kTooltipTitleWrapPx = 1000.0;
inline constexpr double kTooltipDescriptionWrapPx = 280.0;
inline constexpr size_t kTooltipMaxTextBytes = 4096;
inline constexpr size_t kTooltipMaxLines = 12;

struct TooltipInfo {
  std::string title;
  std::string shortcut;     // shown muted after the title, e.g. "Ctrl+Z"
  std::string description;  // second block, muted, wrapped
};

class TooltipContent final : public WidgetObject {
 public:
  static std::span<const theme::StyleRuleEntry> styleRows();

  explicit TooltipContent(TooltipInfo info);
  const char* typeName() const override { return "TooltipContent"; }
  void onAttached() override;
  core::layout::MeasureResult measure(const core::layout::MeasureInput& input) override;
  void paint(PaintContext& ctx) override;

  const TooltipInfo& info() const { return info_; }

 private:
  struct Lines {
    std::vector<std::string> title;
    std::vector<std::string> description;
    double shortcutWidth = 0.0;
    double titleWidth = 0.0;
    double descriptionWidth = 0.0;
    float scale = 0.0f;
  };
  const Lines& lines() const;

  TooltipInfo info_;
  mutable Lines lines_;
};

// Splits `text` into lines no wider than `maxWidth` logical px (hard breaks at '\n', soft breaks at
// spaces, words wider than a line are cut at code point boundaries). At most `maxLines` lines; the
// last one ends with U+2026 when text was dropped. Exposed for tests.
std::vector<std::string> wrapTooltipText(UiContext& ui, std::string_view text, double fontSize, int weight, double maxWidth, size_t maxLines);

// Per-context registry of rich tooltip content, keyed by source widget.
class RichTooltips {
 public:
  // Installs the content factory and kMeasuredTooltipTiming on ui.tooltips() and returns the registry
  // (the factory keeps it alive as long as the context lives).
  static std::shared_ptr<RichTooltips> install(UiContext& ui);

  // Gives `widget` rich tooltip content (also sets its tooltip text so the manager shows it). False
  // for a stale widget or an empty title.
  bool set(core::tree::WidgetId widget, TooltipInfo info);
  void clear(core::tree::WidgetId widget);
  const TooltipInfo* find(core::tree::WidgetId widget) const;
  size_t size() const { return entries_.size(); }

 private:
  explicit RichTooltips(UiContext& ui) : ui_(&ui) {}
  void purgeDead();

  UiContext* ui_;
  std::unordered_map<core::tree::WidgetId, TooltipInfo> entries_;
};

}  // namespace r1ui::widgets
