// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: golden structural tests of the generated property panel: for fixed sample objects the whole
//   panel is described as text (PropertyPanel::describe: sections, collapse state, per row the kind, the
//   control values, mixed / bound / broken / disabled / read-only flags and the reset affordance) and
//   compared with a golden string, for the mixed, reset, bound, broken, disabled, filtered and empty
//   states. A mismatch prints the actual text so the golden can be reviewed and updated deliberately.
// Callers: CTest (props fast, no GPU).
#include <string>

#include "../../ui-props/support/Samples.h"
#include "../textinput/FieldRig.h"
#include "r1ui/widgets/props/PropertyPanel.h"

namespace {

using namespace r1ui::widgets;
using namespace samples;
namespace layout = r1ui::core::layout;

struct Rig {
  r1test::FieldRig ui{300, 900};
  PropertyContext context;
  PanelState state;
  PropertyPanel* panel = nullptr;
  explicit Rig(ContextOptions options = {}) : context(std::move(options)) {}
  void mount() {
    ui.ui.rootStyle().alignItems = layout::Align::Stretch;
    panel = &ui.ui.create<PropertyPanel>(ui.ui.root(), context, state);
    panel->style().width = layout::Length::px(258);
    ui.layout();
  }
  void settle() {
    ui.ui.setTime(ui.ui.now() + 20);
    ui.ui.tick();
    ui.layout();
  }
};

void expectGolden(const char* name, const std::string& actual, const std::string& golden) {
  const bool same = actual == golden;
  r1test::report(same, name, __FILE__, __LINE__);
  if (!same) std::fprintf(stderr, "---- %s: actual ----\n%s---- expected ----\n%s----\n", name, actual.c_str(), golden.c_str());
}

class Variables final : public BindingProvider {
 public:
  std::optional<Value> resolve(std::string_view s) const override { return s == "key" ? std::optional<Value>(250.0) : std::nullopt; }
  std::vector<VariableInfo> variables() const override { return {{"key", ValueKind::Double, {}}}; }
};

void mixedState() {
  Rig rig;
  std::vector<Material> mats(2);
  mats[0].roughness = 0.2;
  mats[1].roughness = 0.8;
  mats[1].color = Color{0.75f, 0.45f, 0.3f, 1.0f};
  mats[1].shading = Shading::Toon;
  mats[1].useTexture = true;
  mats[0].name = "Steel";
  mats[1].name = "Clay";
  rig.context.setSelection(std::vector<Target>{targetOf(mats[0], materialSet()), targetOf(mats[1], materialSet())});
  rig.mount();
  expectGolden("mixed", rig.panel->describe(), R"(panel search="" advanced=0 searchbox=shown
section "Appearance" open reset=enabled
  "Color" color hex="" (mixed) chip=CCCCCC (mixed) alpha=100 [reset]
  "Roughness" range number=Mixed slider=0.2 (mixed) [reset]
  "Shading" enum select=<none> [reset]
section "Texture" open reset=enabled
  "Use texture" bool switch=mixed [reset]
  "Texture" string text="" (disabled)
  "Texture scale" double number=1
section "Object" open reset=enabled
  "Name" string text="" (mixed) [reset]
)");
}

void resetState() {
  Rig rig;
  Transform t;
  t.position = {141.0, 72.0, 0.0};
  t.scale = {2.0, 1.0, 1.0};
  rig.context.setSelection(std::vector<Target>{targetOf(t, transformSet())});
  rig.mount();
  expectGolden("reset-visible", rig.panel->describe(), R"(panel search="" advanced=0 searchbox=shown
section "Transform" open reset=enabled
  "Position" vec3 X=141 Y=72 Z=0 [reset]
  "Rotation" vec3 X=0 Y=0 Z=0
  "Scale" vec3 X=2 Y=1 Z=1 [reset]
section "Object" open reset=disabled
  "Name" string text="Object"
  "Visible" bool checkbox=on
)");
  rig.context.resetCategory("Transform");
  rig.settle();
  expectGolden("reset-done", rig.panel->describe(), R"(panel search="" advanced=0 searchbox=shown
section "Transform" open reset=disabled
  "Position" vec3 X=0 Y=0 Z=0
  "Rotation" vec3 X=0 Y=0 Z=0
  "Scale" vec3 X=1 Y=1 Z=1
section "Object" open reset=disabled
  "Name" string text="Object"
  "Visible" bool checkbox=on
)");
}

void boundState() {
  Variables vars;
  ContextOptions options;
  options.provider = &vars;
  Rig rig(options);
  Light a, b;
  a.name = "Key";
  b.name = "Fill";
  a.setIntensity(250.0);
  rig.context.bindings().set(&a, "intensity", BindingRecord{"key"});
  rig.context.bindings().set(&b, "intensity", BindingRecord{"gone"});
  rig.context.setSelection(std::vector<Target>{targetOf(a, lightSet())});
  rig.mount();
  expectGolden("bound", rig.panel->describe(), R"(panel search="" advanced=0 searchbox=shown
section "Light" open reset=enabled
  "Intensity" double number=250 bound:key [reset]
  "Unit" enum select=Lumen
  "Radius" double number=1
  "Cast shadows" bool checkbox=on
  "Color" color hex="FFFFFF" chip=FFFFFF alpha=100
section "Object" open reset=enabled
  "Name" string text="Key" [reset]
)");
  rig.context.setSelection(std::vector<Target>{targetOf(b, lightSet())});
  rig.settle();
  expectGolden("broken", rig.panel->describe(), R"(panel search="" advanced=0 searchbox=shown
section "Light" open reset=enabled
  "Intensity" double number=100 bound:gone(broken) [reset]
  "Unit" enum select=Lumen
  "Radius" double number=1
  "Cast shadows" bool checkbox=on
  "Color" color hex="FFFFFF" chip=FFFFFF alpha=100
section "Object" open reset=enabled
  "Name" string text="Fill" [reset]
)");
  rig.context.setSelection(std::vector<Target>{targetOf(a, lightSet()), targetOf(b, lightSet())});
  rig.settle();
  expectGolden("binding-mixed", rig.panel->describe(), R"(panel search="" advanced=0 searchbox=shown
section "Light" open reset=enabled
  "Intensity" double number=Mixed [reset] [binding mixed]
  "Unit" enum select=Lumen
  "Radius" double number=1
  "Cast shadows" bool checkbox=on
  "Color" color hex="FFFFFF" chip=FFFFFF alpha=100
section "Object" open reset=enabled
  "Name" string text="" (mixed) [reset]
)");
}

void disabledAndFiltered() {
  Rig rig;
  Material m;
  m.secret = "x";
  rig.context.setSelection(std::vector<Target>{targetOf(m, materialSet())});
  rig.mount();
  expectGolden("disabled", rig.panel->describe(), R"(panel search="" advanced=0 searchbox=shown
section "Appearance" open reset=disabled
  "Color" color hex="CCCCCC" chip=CCCCCC alpha=100
  "Roughness" range number=0.50 slider=0.5
  "Shading" enum select=Smooth
section "Texture" open reset=disabled
  "Use texture" bool switch=off
  "Texture" string text="" (disabled)
section "Object" open reset=disabled
  "Name" string text="Material"
)");
  rig.panel->setSearchText("Tex");
  expectGolden("filtered", rig.panel->describe(), R"(panel search="Tex" advanced=0 searchbox=shown
section "Texture" open reset=disabled
  "Use texture" bool switch=off
  "Texture" string text="" (disabled)
)");
  rig.panel->setSearchText("zzz");
  expectGolden("no-match", rig.panel->describe(), R"(panel search="zzz" advanced=0 searchbox=shown
notice "No matching properties"
)");
  rig.panel->setSearchText("");
  rig.state.setShowAdvanced(true);
  rig.context.setSelection(std::vector<Target>{});
  rig.settle();
  expectGolden("empty", rig.panel->describe(), R"(panel search="" advanced=1 searchbox=hidden
notice "Nothing selected"
)");
}

}  // namespace

int main() try {
  mixedState();
  resetState();
  boundState();
  disabledAndFiltered();
  return r1test::finish();
} catch (const std::exception& e) {
  std::fprintf(stderr, "uncaught exception: %s\n", e.what());
  return 2;
}
