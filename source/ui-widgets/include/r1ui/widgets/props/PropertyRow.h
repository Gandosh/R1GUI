// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: PropertyRowView, the widget generated for one row of a PropertyContext: a caption line (the
//   property label, the always-reserved reset affordance) and the controls its kind needs; the wiring
//   from those controls to PropertyContext edits (one undo step per gesture); and the translation of
//   PropertyState (mixed, default, enabled, read-only, binding) into widget states.
// Why: spec 09 rules 10-52 and 63-67 and D16: every property edits the same way whatever its type, so the
//   mapping kind -> widget lives in one place. Controls by kind: Int/Double a NumberField (scrub, units,
//   soft range, increment, variable button); Range a PropSlider plus a NumberField; Bool a Checkbox
//   (Switch with asSwitch); Enum a Select; String a TextInput; Color a ColorChip (opens a ColorPicker
//   popover), a hex TextInput and an alpha NumberField; Vec2/Vec3 NumberFields labelled X, Y, Z.
// Mixed display: NumberField, Checkbox, Switch, TextInput and ColorChip use their kMixed state; a Select
//   shows the placeholder "Mixed" with no entry selected; a slider rests its thumb in the middle.
// Binding (D16): numeric rows show the variable button; a bound row shows the variable pill (the
//   component colour), a broken one the same pill in the invalid look with a tooltip naming the missing
//   variable. Rows of other kinds can be bound through PropertyContext but show no link affordance yet.
// Interaction bracket: a NumberField / slider / colour gesture is begun with beginInteraction and ended
//   with endInteraction (cancelInteraction on Escape), so a scrub is one undo step; discrete controls
//   (checkbox, select, text commit, reset) edit once.
// Callers: PropertyPanel, tests, the gallery. Calls: PropertyContext. Lifetime: the context must outlive
//   the view; the view holds WidgetIds, never pointers, and tolerates a stale row index (no-ops).
// Row menu: a right click on the row (outside a control that uses it) offers copy and paste of the value as
//   text through the host clipboard, reset, and copying the stored property name (spec 09 rules 50, 68-70, 73).
// Not implemented: shift-click copy and paste, the category and group copy/paste menus (the headless
//   PropertyContext has them), multi-line text editing (a single-line TextInput is used), password masking while
//   typing, the inline enable-condition checkbox (rule 65), array/struct rows.
#pragma once

#include <string>
#include <vector>

#include "r1ui/props/PropertyContext.h"
#include "r1ui/widgets/popover/Popover.h"
#include "r1ui/widgets/runtime/WidgetObject.h"

namespace r1ui::widgets {

class Checkbox;
class NumberField;
class PropResetButton;
class PropSlider;
class ColorChip;
class Select;
class Switch;
class TextInput;

// User-visible words of the generated controls; the host replaces them to localise.
struct PropertyStrings {
  std::string reset = "Reset to default";
  std::string mixed = "Mixed";
  std::string unbind = "Unbind";
  std::string noVariables = "No variables";
  std::string brokenPrefix = "Missing variable: ";
  std::string hiddenValue = "Hidden";
  std::string applyVariable = "Bind to a variable";
  std::string copyValue = "Copy value";
  std::string pasteValue = "Paste value";
  std::string resetMenu = "Reset to default";
  std::string copyName = "Copy property name";
};

struct PropertyRowOptions {
  bool caption = true;        // false: only the controls (cropped comparisons, compact hosts)
  bool mergeSteps = false;    // one-notch gestures (wheel, arrow, typed commit) group within the undo window
  PropertyStrings strings;
};

class PropertyRowView : public WidgetObject {
 public:
  PropertyRowView(props::PropertyContext& context, size_t row, PropertyRowOptions options = {});
  ~PropertyRowView() override;

  const char* typeName() const override { return "PropertyRowView"; }
  void onAttached() override;
  void onDetached() override;
  void onClick(Event& e) override;  // a right click opens the row menu
  std::string_view accessibleName() const override;

  size_t row() const { return row_; }
  props::ValueKind kind() const { return kind_; }
  // Pushes the context's current state into the controls (no callbacks, no undo steps).
  void refresh();
  // The outcome of the last edit this row made (for status lines and tests).
  const props::EditReport& lastReport() const { return report_; }
  // One line describing the row's visible state, for golden tests and diagnostics.
  std::string describe() const;

  // ---- parts (null when the kind has none) ----
  NumberField* number(size_t axis = 0) const;
  size_t numberCount() const { return numbers_.size(); }
  NumberField* alpha() const;   // the opacity field of a colour row
  PropSlider* slider() const;
  Checkbox* checkbox() const;
  Switch* toggle() const;
  Select* select() const;
  TextInput* text() const;   // the string editor, or the hex field of a colour row
  ColorChip* chip() const;
  PropResetButton* resetButton() const;
  bool colorPopupOpen() const;
  // Opens the colour popover as a click on the chip does; the variable flyout likewise.
  void openColorPopup();
  void openBindingMenu();
  // The row menu (copy, paste, reset, copy name) at a window position; the clipboard is the UiContext host's.
  void openRowMenu(double x, double y);

 private:
  using Id = core::tree::WidgetId;

  void build();
  void buildNumbers(Id parent, size_t count, bool withSlider);
  void buildBool(Id captionLine);
  void buildEnum(Id parent);
  void buildString(Id parent);
  void buildColor(Id parent);
  void configureNumber(NumberField& field, int axis);
  void refreshNumbers(const props::PropertyState& state, bool editable);
  void refreshOthers(const props::PropertyState& state, bool editable);
  // Gesture bracket shared by the continuous controls.
  void beginGesture();
  void endGesture(bool cancelled);
  void edit(int axis, double value, bool interactive = false);
  void afterEdit();

  props::PropertyContext* ctx_;
  size_t row_;
  PropertyRowOptions options_;
  props::ValueKind kind_ = props::ValueKind::Double;
  bool begun_ = false;
  bool scrubbed_ = false;  // the open gesture applied interactive (drag) values
  bool syncText_ = false;  // the next refresh may replace the text of a focused field (after a commit)
  props::EditReport report_;
  Id caption_, reset_, slider_, check_, toggle_, select_, text_, chip_, alpha_;
  std::vector<Id> numbers_;
  PopoverHandle colorPopup_;
};

}  // namespace r1ui::widgets
