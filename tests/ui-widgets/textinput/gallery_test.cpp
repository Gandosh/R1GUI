// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: smoke oracle for the field gallery page: it builds in a headless window at several scales and
//   in both themes, lays out, paints, and its selects open real popups from the page.
// Callers: CTest (textinput fast, no GPU).
#include <string>

#include "FieldRig.h"
#include "r1ui/widgets/numberfield/NumberField.h"
#include "r1ui/widgets/select/Select.h"
#include "r1ui/widgets/textinput/GalleryFields.h"
#include "r1ui/widgets/textinput/TextInput.h"

namespace {

using namespace r1ui::widgets;
using r1ui::core::tree::WidgetId;

size_t countType(UiContext& ui, WidgetId id, const std::string& type) {
  size_t n = ui.object(id) != nullptr && type == ui.object(id)->typeName() ? 1 : 0;
  for (WidgetId c = ui.tree().firstChild(id); c.valid(); c = ui.tree().nextSibling(c)) n += countType(ui, c, type);
  return n;
}

Select* firstEnabledSelect(UiContext& ui, WidgetId id) {
  if (Select* s = ui.objectAs<Select>(id); s != nullptr && s->enabled()) return s;
  for (WidgetId c = ui.tree().firstChild(id); c.valid(); c = ui.tree().nextSibling(c)) {
    if (Select* s = firstEnabledSelect(ui, c)) return s;
  }
  return nullptr;
}

}  // namespace

int main() {
  for (const float scale : {1.0f, 1.5f, 2.0f}) {
    for (const auto theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
      r1test::FieldRig rig(1000, 900, scale);
      rig.services.theme().set(theme);
      buildGalleryFields(rig.ui, rig.ui.root());
      rig.layout();
      R1_EXPECT(countType(rig.ui, rig.ui.root(), "TextInput") >= 32);
      R1_EXPECT(countType(rig.ui, rig.ui.root(), "NumberField") >= 8);
      R1_EXPECT(countType(rig.ui, rig.ui.root(), "Select") >= 6);
      R1_EXPECT(rig.paint() > 100);
      Select* select = firstEnabledSelect(rig.ui, rig.ui.root());
      R1_EXPECT(select != nullptr);
      if (select != nullptr) {
        R1_EXPECT(select->open());
        rig.layout();
        R1_EXPECT(select->isOpen() && rig.paint() > 100);
        select->close();
      }
    }
  }
  return r1test::finish();
}
