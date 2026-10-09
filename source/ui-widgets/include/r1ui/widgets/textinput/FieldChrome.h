// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the look shared by TextInput, NumberField and Select: the style rows of the whole field group
//   (input.*, numberfield.*, select.*), the animated box (background and border colour transitions of
//   150 ms) and the selection colours of text fields.
// Why: all three widgets draw the same field surface (spec 2.3, 2.4, 2.6) with the same hover,
//   focus, invalid and disabled behaviour; keeping rows and drawing in one place keeps them in step
//   and lets each widget register the same table (Services registers a table once).
// Callers: TextInput, NumberField, Select (their styleRows() all return fieldStyleRows()).
// Row sources: docs/spec/widgets.md sections 2.3 (default tone: bg input, border `border`, focus
//   border `accent`; panel tone: bg panel-field, hover panel-field-hover, focus border panel-focus),
//   2.4, 2.6 and docs/spec/measurements*.json. The text selection colours are the browser highlight
//   measured in the reference crops (dark #0c40aa, light #2e63cd, white text), because no token
//   describes them; they are the only literal colours of the group.
#pragma once

#include <span>

#include "r1ui/render/Painter.h"
#include "r1ui/theme/StyleSheet.h"
#include "r1ui/widgets/runtime/PaintContext.h"

namespace r1ui::widgets {

// All rows of the field group (one table, registered once per process).
std::span<const theme::StyleRuleEntry> fieldStyleRows();

struct FieldColors {
  render::Color background;
  render::Color border;
};

// Colours to draw now for `rs`, moving towards its targets over the token duration; uses animation
// slots baseSlot and baseSlot + 1 of the widget.
FieldColors animatedFieldColors(PaintContext& ctx, const theme::ResolvedStyle& rs, int baseSlot);

// Background and inside border of `box` with the given colours and the radius / border width of `rs`.
void paintFieldBox(PaintContext& ctx, const theme::ResolvedStyle& rs, const FieldColors& colors, const render::Rect& box);

// Highlight behind selected text and the text colour inside it, for the active theme.
render::Color fieldSelectionBackground(PaintContext& ctx);
render::Color fieldSelectionText(PaintContext& ctx);

}  // namespace r1ui::widgets
