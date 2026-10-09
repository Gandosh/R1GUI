// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: GradientEditor, the content of the gradient tab of the fill popover (measured in
//   screen-gradient-editor): the mode strip, the type control (a select, or three buttons), optional
//   angle / centre fields, the gradient bar with its stop handles, the "Stops" header with the add
//   button, one row per stop, and a colour picker (without chrome or tabs) editing the selected
//   stop's colour.
// Why: one widget that every consumer mounts, with one data contract: setGradient in, onChanged out
//   with the whole gradient, bracketed by begin / end callbacks so every gesture (a bar drag, a typed
//   field, a picker drag) is exactly one undo step; selection is kept by stop id.
// Callers: application code (inside a Popover or any container), tests, the gallery. Calls: the
//   gradient and colour picker parts.
// Contract: setGradient / setSelectedStop never call back. User edits call onBeginInteraction once
//   per gesture, then onChanged(GradientChange) for every change (interactive = true while the
//   gesture is open), then onEndInteraction; selection changes alone call onSelectionChanged.
//   Gradient edits never drop below two stops or above kMaxStops. Escape during a bar drag restores
//   the gradient the drag started with.
// Geometry (logical px): content 222 wide; with chrome 240 (padding 8 + border 1, like ColorPicker).
//   Type select 112 x 26, bar 222 x 24 (handles overshoot by 7 px), "Stops" header 16 high with a
//   16 px add button, rows 222 x 30, then the picker body.
// Interpolation preview: the bar and the stop swatches show exactly what Gradient::evaluate returns
//   (premultiplied sRGB interpolation over a checkerboard), which is what a renderer should apply.
#pragma once

#include <functional>
#include <vector>

#include "r1ui/widgets/colorpicker/ColorPicker.h"
#include "r1ui/widgets/gradient/GradientBar.h"
#include "r1ui/widgets/gradient/GradientModel.h"
#include "r1ui/widgets/gradient/GradientStopRow.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

struct GradientChange {
  gradient::Gradient gradient;
  uint32_t selectedId = 0;
  bool interactive = false;
};

class GradientEditor : public WidgetObject {
 public:
  enum class TypeControl : uint8_t { Select, Buttons };

  const char* typeName() const override { return "GradientEditor"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  std::string_view accessibleName() const override;

  // ---- value ----
  void setGradient(const gradient::Gradient& g);
  const gradient::Gradient& gradient() const { return model_; }
  uint32_t selectedStopId() const { return selected_; }
  void setSelectedStop(uint32_t id);

  // ---- look ----
  void setChrome(bool chrome);
  void setShowModeTabs(bool show);
  void setMode(PickerMode mode);
  void setTypeControl(TypeControl control);
  // Angle and centre fields under the type control (hidden fields do not take space).
  void setShowGeometryFields(bool show);

  // ---- parts (tests, gallery) ----
  GradientBar& bar() const;
  ColorPicker& picker() const;
  PickerDropdown& typeSelect() const;
  PickerButton& typeButton(gradient::GradientType type) const;
  PickerButton& addStopButton() const;
  PickerButton& modeTab(PickerMode mode) const;
  PickerEntry& angleEntry() const;
  PickerEntry& centerXEntry() const;
  PickerEntry& centerYEntry() const;
  size_t rowCount() const { return rows_.size(); }
  GradientStopRow& row(size_t index) const;
  bool gestureOpen() const { return gestureDepth_ > 0; }

  // ---- callbacks ----
  std::function<void()> onBeginInteraction;
  std::function<void(const GradientChange&)> onChanged;
  std::function<void()> onEndInteraction;
  std::function<void(uint32_t id)> onSelectionChanged;
  std::function<void(PickerMode)> onModeChanged;

 private:
  void beginGesture();
  void endGesture();
  void emit();
  void rebuildRows();
  void refreshAll(bool refreshPicker);
  void refreshGeometry();
  void selectStop(uint32_t id);
  void addStopAtGap();
  void applyType(gradient::GradientType type);
  bool commitGeometry(int field, std::string_view text);
  // Applies `change` to a copy of the model; when it changed something the parts are refreshed and
  // onChanged is called. edit() brackets that in one gesture, apply() expects the caller's bracket.
  bool apply(const std::function<bool(gradient::Gradient&)>& change, bool refreshPicker);
  void commitModel(gradient::Gradient next, bool refreshPicker);
  void edit(const std::function<bool(gradient::Gradient&)>& change);

  gradient::Gradient model_;
  uint32_t selected_ = 0;
  PickerMode mode_ = PickerMode::Gradient;
  TypeControl typeControl_ = TypeControl::Select;
  bool chrome_ = true;
  bool showGeometry_ = true;
  int gestureDepth_ = 0;
  core::tree::WidgetId tabs_, tabButtons_[3], typeRow_, typeSelect_, typeButtons_[3], geometryRow_, angle_, centerX_, centerY_, bar_, header_, add_,
      stopsBox_, rowsBox_, picker_;
  std::vector<core::tree::WidgetId> rows_;
};

}  // namespace r1ui::widgets
