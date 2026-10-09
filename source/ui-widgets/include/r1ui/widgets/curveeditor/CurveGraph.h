// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CurveGraph, the graph area of the curve editor (spec 11): a time ruler, the grid with its
//   labels, any number of curves in their colours, key and tangent handle markers, the hovered curve,
//   the scrub time marker, value indicators and the marquee; and all pointer and keyboard editing of
//   keys: view pan and zoom, selection (click, modifiers, marquee, select all / invert), key
//   insertion (middle click, double click, Enter, ctrl for shape keeping), dragging keys and tangent
//   handles with axis locking and snapping, nudging, delete, copy / cut / paste, interpolation and
//   tangent hotkeys, framing, and the begin / change / end interaction callbacks for undo.
// Why: the widget is what the owner judges; every rule of the spec that is about the graph itself
//   lives here, on top of the pure modules (CurveMath, CurveView, CurveOps) that carry the maths.
// Callers: CurveEditor (composite), tests, the gallery. Calls: CurveMath, CurveView, CurveOps,
//   PaintContext (text and Painter).
// Data ownership: the graph keeps a copy of the curves (setCurves replaces them silently); every user
//   edit changes that copy and is reported. The host mirrors the copy into its own model on
//   onChanged, or on onEndInteraction(true).
// Undo grouping: a user gesture opens one interaction at its FIRST modification
//   (onBeginInteraction(label)), reports every intermediate state with onChanged, and closes it with
//   onEndInteraction(true) on release, or onEndInteraction(false) after Escape / capture loss restored
//   the curves the gesture started with. Selection changes alone never open an interaction (they call
//   onSelectionChanged). A middle-click insertion that continues into a drag is one interaction,
//   labelled "Insert and move key"; pressing a key and dragging past the threshold is "Move keys".
// Coordinates: logical pixels; the plot is the widget box below the 24 px ruler strip.
// Performance: drawing visits only the keys and segments inside the visible time range (found by
//   binary search); cubic segments are flattened to 1 px tolerance in screen space; when more than one
//   key falls on a pixel column the polyline is reduced to that column's envelope, so the cost depends
//   on the plot size, not on the key count (10 000 keys redraw well inside 16 ms).
// Not implemented (see Goal/evidence/P4_g5_notes.md): bracket-key selection shortcuts (the Key enum
//   has no brackets; selectAfterScrub / selectBeforeScrub are callable), selection range markers
//   (I / O), view arrangements other than one shared graph, stored curve copies (ghosts), tangent
//   visibility levels beyond selected / all / none.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "r1ui/widgets/curveeditor/CurveOps.h"
#include "r1ui/widgets/curveeditor/CurveView.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

enum class AxisLock : uint8_t { None, Horizontal, Vertical };
enum class TangentVisibility : uint8_t { None, Selected, All };

struct CurveSettings {
  double framesPerSecond = 30.0;
  bool snapTime = false;                 // new and moved key times snap to whole frames
  bool snapValue = false;                // values snap to valueSnapStep
  double valueSnapStep = 0.0;            // 0 = the minor grid step of the current view
  AxisLock axisLock = AxisLock::None;    // a chosen lock applies without Shift (rule 47)
  double wheelMultiplier = 1.0;          // 0.05 .. 32
  bool zoomAtScrubTime = false;          // wheel zooms about the scrub time when it is visible (rule 3)
  curve::ZoomLimits zoomLimits;
  TangentVisibility tangents = TangentVisibility::Selected;
  bool curveTooltip = true;
  bool valueIndicators = true;           // dotted lines at the lowest / highest selected value
};

// What a context-menu request points at.
struct CurveContext {
  enum class Target : uint8_t { Empty, Key, Curve } target = Target::Empty;
  double x = 0.0;   // window coordinates (logical px)
  double y = 0.0;
  double time = 0.0;
  double value = 0.0;
  uint32_t curveId = 0;
  uint32_t keyId = 0;
};

// The summary the key fields show for the selected keys.
struct SelectionInfo {
  size_t keyCount = 0;
  size_t curveCount = 0;
  bool mixedTime = false;
  bool mixedValue = false;
  bool mixedInterp = false;
  bool mixedTangent = false;
  bool mixedWeighted = false;
  double time = 0.0;      // of the first selected key
  double value = 0.0;
  curve::Interp interp = curve::Interp::Linear;
  curve::TangentMode tangent = curve::TangentMode::AutoSmooth;
  bool weighted = false;
};

class CurveGraph : public WidgetObject {
 public:
  static constexpr double kRulerHeight = 24.0;
  static constexpr double kHoverDistance = 5.0;       // rule 24
  static constexpr double kKeySize = 7.0;             // drawn marker
  static constexpr double kKeyHitSlop = 4.0;          // rule: drawn size plus 4
  static constexpr double kHandleLength = 36.0;
  static constexpr double kHandleHit = 6.0;

  const char* typeName() const override { return "CurveGraph"; }
  void onAttached() override;
  void paint(PaintContext& ctx) override;
  void paintOver(PaintContext& ctx) override;
  Cursor cursor() const override;
  std::string_view tooltipText() const override;
  std::string_view accessibleName() const override;

  // ---- data (silent) ----
  void setCurves(std::vector<curve::Curve> curves);  // sanitised copy
  const std::vector<curve::Curve>& curves() const { return curves_; }
  void setSelection(curve::Selection selection);
  const curve::Selection& selection() const { return selection_; }
  void setScrubTime(double t);
  double scrubTime() const { return scrubTime_; }
  void setSettings(const CurveSettings& s);
  const CurveSettings& settings() const { return settings_; }
  void setAxisLock(AxisLock lock) { settings_.axisLock = lock; }

  // ---- view ----
  const curve::View& view() const { return view_; }
  void setView(const curve::View& v);
  // Rule 7 / 9 / 8: fit the selected keys (all visible curves when none), all curves, or one axis.
  void frameSelected();
  void frameAll();
  void frameHorizontal();
  void frameVertical();
  curve::Mapping mapping() const;   // plot rectangle in widget-local logical pixels
  double plotWidth() const;
  double plotHeight() const;

  // ---- selection commands (rules 35, 36) ----
  void selectAll();
  void clearSelection();
  void invertSelection();
  void selectAfterScrub();
  void selectBeforeScrub();
  SelectionInfo selectionInfo() const;

  // ---- edit commands, each one undo step (also reachable from the keyboard) ----
  void addKeysAtScrubTime();                       // Enter
  void deleteSelected();                           // Delete
  void copySelected();
  void cutSelected();
  void paste(curve::PasteMode mode);
  bool hasClipboard() const { return !clipboard_.empty(); }
  void nudgeSelected(double dt, double dv, const char* label);
  void setSelectedInterpolation(curve::Interp interp);
  void setSelectedTangentMode(curve::TangentMode mode);
  void toggleWeights();
  void flattenSelectedTangents();
  void straightenSelectedTangents();
  void snapSelectedToFrames();
  void flipSelectedCurvesHorizontal();
  void flipSelectedCurvesVertical();
  void setSelectedTime(double time);               // exact entry, rule 64
  void setSelectedValue(double value);
  void setExtrapolation(uint32_t curveId, bool post, curve::Extrapolation mode);
  void matchLoop(uint32_t curveId, bool fromStart);

  // ---- state for tests and the host ----
  bool interactionOpen() const { return interactionOpen_; }
  bool dragging() const { return gesture_.kind != Gesture::Kind::None && gesture_.active; }
  uint32_t hoveredCurve() const { return hoverCurve_; }
  // Screen position (widget-local logical px) of a key or a tangent handle, for tests.
  curve::Point keyPosition(uint32_t curveId, uint32_t keyId) const;
  bool handlePosition(uint32_t curveId, uint32_t keyId, bool outSide, curve::Point& out) const;
  // Same for the key at `index` of `curve` (O(1); the id based overload searches the curve and the key).
  bool handlePositionAt(const curve::Curve& curve, size_t index, bool outSide, curve::Point& out) const;
  size_t lastDrawnKeyCount() const { return drawnKeys_; }
  size_t lastDrawnPointCount() const { return drawnPoints_; }

  // ---- events ----
  void onPointerDown(Event& e) override;
  void onPointerMove(Event& e) override;
  void onPointerUp(Event& e) override;
  void onPointerWheel(Event& e) override;
  void onDoubleClick(Event& e) override;
  void onCaptureLost(Event& e) override;
  void onPointerLeave(Event& e) override;
  void onKeyDown(Event& e) override;

  // ---- callbacks ----
  std::function<void(const std::string& label)> onBeginInteraction;
  std::function<void(const std::vector<uint32_t>& changedCurves)> onChanged;
  std::function<void(bool committed)> onEndInteraction;
  std::function<void()> onSelectionChanged;
  std::function<void()> onViewChanged;
  std::function<void(double time)> onScrubChanged;
  std::function<void(const CurveContext&)> onContextMenu;

 private:
  struct Gesture {
    enum class Kind : uint8_t { None, Press, Marquee, MoveKeys, MoveHandle, InsertDrag, Pan, Zoom, Scrub, ContextPending };
    Kind kind = Kind::None;
    bool active = false;          // the drag threshold has been passed
    core::events::Button button = core::events::Button::None;
    double startX = 0.0;          // widget-local logical px
    double startY = 0.0;
    double startTime = 0.0;
    double startValue = 0.0;
    uint8_t modifiers = 0;
    curve::Selected item;         // the pressed key or handle
    bool itemWasSelected = false;
    curve::View startView;
    // Drag of keys / handles.
    curve::Point itemStart;       // data position of the pressed item
    double handleOffsetT = 0.0;   // pointer offset from the handle, data units
    double handleOffsetV = 0.0;
    int axis = 0;                 // 0 free, 1 horizontal, 2 vertical (dominant axis lock)
    bool shiftWasDown = false;
    // Marquee.
    double marqueeX = 0.0;
    double marqueeY = 0.0;
  };

  // ---- geometry ----
  curve::Mapping plotMapping() const;
  bool curveEditable(const curve::Curve& c) const;
  bool curveDrawn(const curve::Curve& c) const;
  double valueStep() const;

  // ---- hit testing ----
  curve::Selected hitItem(double x, double y) const;    // key / handle under the point, curve = 0 when none
  uint32_t hitCurve(double x, double y, double* timeOut = nullptr, double* valueOut = nullptr) const;
  void updateHover(double x, double y);

  // ---- selection helpers ----
  void applySelection(const curve::Selected& item, uint8_t modifiers);
  void setSelectionInternal(curve::Selection s);
  void notifySelection();

  // ---- interactions ----
  void beginInteraction(const std::string& label);
  void touch(uint32_t curveId);                       // snapshot before the first change of a curve
  void changedCurves(const std::vector<uint32_t>& ids);
  void endInteraction(bool committed);
  void cancelGesture();
  void finishGesture(Event* e);
  void startKeyDrag();
  void updateKeyDrag(double x, double y, uint8_t modifiers);
  void updateHandleDrag(double x, double y);
  void insertAt(double x, double y, bool keepShape, bool beginDrag);
  bool dragExceeded(double x, double y) const;
  void applyMarquee(uint8_t modifiers);
  void zoomWheel(double x, double y, double notches, uint8_t modifiers);
  void setViewInternal(const curve::View& v);
  std::vector<uint32_t> selectedCurveIds() const;
  // Applies `op` to a copy of each editable curve that has selected keys (or, with ownersToo and no
  // selected key, selected tangent handles) and commits the changed ones as one interaction.
  void editSelectedKeys(const char* label, const std::function<bool(curve::Curve&, const std::vector<uint32_t>&)>& op, bool ownersToo = false);

  // ---- painting (CurveGraphPaint.cpp) ----
  void paintGrid(PaintContext& ctx, const curve::Mapping& m);
  void paintCurve(PaintContext& ctx, const curve::Mapping& m, const curve::Curve& c, bool emphasised);
  // handlesPass: tangent handles of the curve; otherwise its key markers (the caller does all handles first).
  void paintKeys(PaintContext& ctx, const curve::Mapping& m, const curve::Curve& c, bool handlesPass);
  void paintIndicators(PaintContext& ctx, const curve::Mapping& m);  // value indicators and the scrub marker
  void paintMarquee(PaintContext& ctx);
  void curvePolyline(const curve::Mapping& m, const curve::Curve& c, std::vector<curve::Point>& out) const;

  std::vector<curve::Curve> curves_;
  curve::Selection selection_;
  curve::View view_;
  CurveSettings settings_;
  double scrubTime_ = 0.0;
  std::vector<curve::ClipCurve> clipboard_;

  Gesture gesture_;
  uint32_t hoverCurve_ = 0;
  uint32_t hoverKeyCurve_ = 0;
  uint32_t hoverKey_ = 0;
  double pointerX_ = 0.0;
  double pointerY_ = 0.0;
  bool pointerInside_ = false;

  bool interactionOpen_ = false;
  std::vector<curve::Curve> snapshots_;                // copies taken before the first change in an interaction
  curve::Selection selectionAtStart_;
  std::vector<curve::Curve> dragBase_;                 // the curves a key / handle drag applies its total offset to
  curve::Selection dragSelection_;
  mutable std::string tooltipScratch_;

  size_t drawnKeys_ = 0;
  // Lowest and highest value of the selected keys (the dotted indicator lines), recomputed only when the
  // selection or the key data changed: the scan is over every selected key, which is O(N) at 100000 keys.
  uint64_t dataRevision_ = 1;  // bumped whenever key data may have changed (changedCurves, setCurves, restore)
  struct IndicatorCache {
    uint64_t selection = 0;
    uint64_t data = 0;
    bool any = false;
    double lo = 0.0;
    double hi = 0.0;
  } indicators_;
  size_t drawnPoints_ = 0;
  std::vector<curve::Point> pointScratch_;
};

}  // namespace r1ui::widgets
