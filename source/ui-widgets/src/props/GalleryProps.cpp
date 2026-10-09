// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: implementation of GalleryProps.h: the sample object types and their PropertySets, a sample
//   binding provider, and the GalleryPropsPage widget that owns the sample data, the PropertyContext and
//   PanelState, the selection controls and the undo/redo buttons.
// Why: the gallery must show the generated panel working on real declarations: mixed values (two objects
//   with different values), edit conditions (the texture rows), a bound and a broken binding, a slider,
//   an enum, a colour, units and advanced rows, all through the public ui-props API.
// Invariants: the page destroys its panel before its sample data dies (onDetached destroys children
//   first); the undo listener is removed in onDetached; only layout boxes, Labels, Segmented,
//   IconButtons and the panel are created, all colours come from rows.
// Callers: the gallery preview, tests.
#include "r1ui/widgets/props/GalleryProps.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>

#include "r1ui/props/PanelState.h"
#include "r1ui/props/PropertyContext.h"
#include "r1ui/widgets/iconbutton/IconButton.h"
#include "r1ui/widgets/label/Label.h"
#include "r1ui/widgets/props/PropertyPanel.h"
#include "r1ui/widgets/runtime/UiContext.h"
#include "r1ui/widgets/section/Section.h"
#include "r1ui/widgets/segmented/Segmented.h"

namespace r1ui::widgets {

namespace {

namespace layout = core::layout;
using core::tree::WidgetId;
using namespace r1ui::props;

constexpr double kPanelWidth = 258.0;   // the reference properties panel
constexpr double kPanelHeight = 560.0;

// ---- sample data -------------------------------------------------------------------------------------------------

struct SampleTransform {
  Vec3 position{};
  Vec3 rotation{};
  Vec3 scale{1.0, 1.0, 1.0};
  bool visible = true;
  std::string name = "Object";
  int layer = 0;
};

enum class Shading { Flat, Smooth, Toon };

struct SampleMaterial {
  Color color{0.8f, 0.8f, 0.8f, 1.0f};
  double roughness = 0.5;
  Shading shading = Shading::Smooth;
  bool useTexture = false;
  std::string texture;
  double textureScale = 1.0;
  std::string name = "Material";
};

enum class LightUnit { Lumen, Candela, Lux };

class SampleLight {
 public:
  double intensity() const { return intensity_; }
  void setIntensity(double v) { intensity_ = v; }
  LightUnit unit = LightUnit::Lumen;
  float radius = 1.0f;
  bool castShadows = true;
  Color color{1.0f, 1.0f, 1.0f, 1.0f};
  std::string name = "Light";

 private:
  double intensity_ = 100.0;
};

const PropertySet<SampleTransform>& transformSet() {
  static const PropertySet<SampleTransform> set = [] {
    PropertySet<SampleTransform> s("Transform");
    s.category("Transform", -10);
    s.add("position", "Position", &SampleTransform::position).unit("m").units({{"cm", 0.01}, {"mm", 0.001}}).range(-1000.0, 1000.0).tooltip("World position");
    s.add("rotation", "Rotation", &SampleTransform::rotation).unit("\xC2\xB0").range(-360.0, 360.0);
    s.add("scale", "Scale", &SampleTransform::scale).range(0.001, 1000.0);
    s.category("Object");
    s.add("name", "Name", &SampleTransform::name).maxLength(64);
    s.add("visible", "Visible", &SampleTransform::visible);
    s.add("layer", "Layer", &SampleTransform::layer).range(0, 31).advanced();
    s.defaultsFrom(SampleTransform{});
    return s;
  }();
  return set;
}

const PropertySet<SampleMaterial>& materialSet() {
  static const PropertySet<SampleMaterial> set = [] {
    PropertySet<SampleMaterial> s("Material");
    s.category("Appearance");
    s.add("color", "Color", &SampleMaterial::color);
    s.add("roughness", "Roughness", &SampleMaterial::roughness).slider(0.0, 1.0).precision(2).tooltip("Surface roughness");
    s.add("shading", "Shading", &SampleMaterial::shading).value("flat", Shading::Flat, "Flat").value("smooth", Shading::Smooth, "Smooth").value("toon", Shading::Toon, "Toon");
    s.category("Texture");
    s.add("useTexture", "Use texture", &SampleMaterial::useTexture).asSwitch();
    s.add("texture", "Texture", &SampleMaterial::texture).enableWhenTrue("useTexture");
    s.add("textureScale", "Texture scale", &SampleMaterial::textureScale).range(0.01, 100.0).visibleWhenTrue("useTexture");
    s.category("Object");
    s.add("name", "Name", &SampleMaterial::name);
    s.defaultsFrom(SampleMaterial{});
    return s;
  }();
  return set;
}

const PropertySet<SampleLight>& lightSet() {
  static const PropertySet<SampleLight> set = [] {
    PropertySet<SampleLight> s("Light");
    s.category("Light");
    s.add("intensity", "Intensity", &SampleLight::intensity, &SampleLight::setIntensity).range(0.0, 1.0e6).softRange(0.0, 1000.0).step(10.0).unit("lm");
    s.add("unit", "Unit", &SampleLight::unit).value("lumen", LightUnit::Lumen, "Lumen").value("candela", LightUnit::Candela, "Candela").value("lux", LightUnit::Lux, "Lux");
    s.add("radius", "Radius", &SampleLight::radius).range(0.0, 100.0);
    s.add("castShadows", "Cast shadows", &SampleLight::castShadows);
    s.add("color", "Color", &SampleLight::color);
    s.category("Object");
    s.add("name", "Name", &SampleLight::name);
    s.defaultsFrom(SampleLight{});
    return s;
  }();
  return set;
}

// The variables the sample host offers; "missing" is bound on purpose and is not defined.
class SampleVariables final : public BindingProvider {
 public:
  std::optional<Value> resolve(std::string_view source) const override {
    const auto it = table_.find(std::string(source));
    if (it == table_.end()) return std::nullopt;
    return it->second;
  }
  std::vector<VariableInfo> variables() const override {
    std::vector<VariableInfo> out;
    for (const auto& [name, value] : table_) out.push_back({name, ValueKind::Double, {}});
    return out;
  }

 private:
  std::map<std::string, Value> table_ = {{"base-roughness", 0.4}, {"key-intensity", 250.0}, {"grid-unit", 1.0}};
};

// ---- the page ------------------------------------------------------------------------------------------------------

struct PageState {
  explicit PageState(const UiContext& ui) : clock(ui), context(makeOptions(*this)) {}
  static ContextOptions makeOptions(PageState& s) {
    ContextOptions o;
    o.provider = &s.variables;
    o.ownedUndo.clock = &s.clock;
    return o;
  }

  UiClock clock;
  SampleVariables variables;
  SampleTransform transforms[2];
  SampleMaterial materials[2];
  SampleLight lights[2];
  PropertyContext context;
  PanelState panelState;
  int type = 0;   // 0 transform, 1 material, 2 light
  int count = 2;  // objects selected
};

class GalleryPropsPage final : public WidgetObject {
 public:
  const char* typeName() const override { return "GalleryPropsPage"; }

  void onAttached() override {
    style().direction = layout::FlexDirection::Column;
    style().gapRow = 12.0;
    style().flexShrink = 0.0;
    state_ = std::make_unique<PageState>(ui());
    seed();

    ui().create<Label>(id(), "Property panel", LabelRole::Title);
    ui().create<Label>(id(), "Generated from declared metadata. Two objects show mixed values: type 'Mixed + 5' in one.", LabelRole::Muted);
    ui().create<Label>(id(), "Scrub a number, click a reset arrow or the variable button, search, then undo from the bar below.", LabelRole::Muted);

    SectionBox& bar = row(id(), 8.0);
    Segmented& type = ui().create<Segmented>(bar.id(), SegmentedSize::Md);
    type.setItems({{.text = "Transform"}, {.text = "Material"}, {.text = "Light"}});
    type.setSelectedIndex(state_->type);
    type.setOnChange([this](int index) { select(index, state_->count); });
    Segmented& count = ui().create<Segmented>(bar.id(), SegmentedSize::Md);
    count.setItems({{.text = "1 object"}, {.text = "2 objects"}});
    count.setSelectedIndex(state_->count - 1);
    count.setOnChange([this](int index) { select(state_->type, index + 1); });

    SectionBox& history = row(id(), 4.0);
    IconButton& undo = ui().create<IconButton>(history.id(), "undo2", IconButtonSize::Md);
    undo.setOnClick([this] { state_->context.undo().undoRouted(); });
    undoButton_ = undo.id();
    IconButton& redo = ui().create<IconButton>(history.id(), "redo2", IconButtonSize::Md);
    redo.setOnClick([this] { state_->context.undo().redoRouted(); });
    redoButton_ = redo.id();
    IconButton& script = ui().create<IconButton>(history.id(), "rotate-cw", IconButtonSize::Md);
    script.setTooltip("Change values from outside (a simulated script)");
    script.setOnClick([this] { externalChange(); });
    Label& status = ui().create<Label>(history.id(), "", LabelRole::Muted);
    status.style().margin[layout::kLeft] = layout::Length::px(8);
    status_ = status.id();

    // The selection is set before the panel exists, so its first build already shows the rows.
    select(state_->type, state_->count);
    SectionBox& frame = ui().create<SectionBox>(id());
    frame.style().width = layout::Length::px(kPanelWidth);
    frame.style().height = layout::Length::px(kPanelHeight);
    frame.style().flexShrink = 0.0;
    PropertyPanel& panel = ui().create<PropertyPanel>(frame.id(), state_->context, state_->panelState);
    panel_ = panel.id();

    listener_ = state_->context.undo().addListener([this](const UndoEvent&) { updateHistory(); });
  }

  void onDetached() override {
    if (state_) state_->context.undo().removeListener(listener_);
    if (ui().alive(panel_)) ui().destroy(panel_);  // before the data it observes goes away
  }

 private:
  SectionBox& row(WidgetId parent, double gap) {
    SectionBox& box = ui().create<SectionBox>(parent);
    box.style().direction = layout::FlexDirection::Row;
    box.style().alignItems = layout::Align::Center;
    box.style().gapColumn = gap;
    box.style().flexShrink = 0.0;
    return box;
  }

  // Sample values chosen so two objects of a type differ in some properties and agree in others, and so
  // one binding is intact and one is broken.
  void seed() {
    PageState& s = *state_;
    s.transforms[0].name = "Cube";
    s.transforms[1].name = "Sphere";
    s.transforms[0].position = {141.0, 72.0, 0.0};
    s.transforms[1].position = {141.0, 12.5, 4.0};
    s.transforms[1].rotation = {0.0, 45.0, 0.0};
    s.materials[0].name = "Steel";
    s.materials[1].name = "Clay";
    s.materials[0].roughness = 0.2;
    s.materials[1].roughness = 0.8;
    s.materials[1].color = Color{0.75f, 0.45f, 0.3f, 1.0f};
    s.materials[1].useTexture = true;
    s.materials[1].texture = "clay.png";
    s.lights[0].name = "Key light";
    s.lights[1].name = "Fill light";
    s.lights[0].setIntensity(250.0);
    s.lights[1].setIntensity(120.0);
    s.lights[1].unit = LightUnit::Candela;
    s.context.bindings().set(&s.materials[0], "roughness", BindingRecord{"base-roughness"});
    s.context.bindings().set(&s.lights[0], "intensity", BindingRecord{"missing"});
    s.context.bindings().set(&s.lights[1], "intensity", BindingRecord{"missing"});
  }

  void select(int type, int count) {
    PageState& s = *state_;
    s.type = type;
    s.count = count;
    std::vector<Target> targets;
    for (int i = 0; i < count; ++i) {
      if (type == 0) targets.push_back(targetOf(s.transforms[i], transformSet()));
      else if (type == 1) targets.push_back(targetOf(s.materials[i], materialSet()));
      else targets.push_back(targetOf(s.lights[i], lightSet()));
    }
    s.context.setSelection(targets);
    updateHistory();
  }

  void externalChange() {
    PageState& s = *state_;
    s.transforms[0].position.x += 10.0;
    s.materials[0].roughness = std::min(1.0, s.materials[0].roughness + 0.1);
    s.lights[0].setIntensity(s.lights[0].intensity() + 50.0);
    s.context.notifyExternalChange();
  }

  void updateHistory() {
    UndoStack& undo = state_->context.undo();
    if (WidgetObject* b = ui().object(undoButton_)) {
      b->setEnabled(undo.canUndo());
      b->setTooltip(undo.canUndo() ? "Undo: " + undo.undoLabel() : std::string("Nothing to undo"));
    }
    if (WidgetObject* b = ui().object(redoButton_)) {
      b->setEnabled(undo.canRedo());
      b->setTooltip(undo.canRedo() ? "Redo: " + undo.redoLabel() : std::string("Nothing to redo"));
    }
    if (Label* l = ui().objectAs<Label>(status_)) {
      l->setText(std::to_string(undo.undoCount()) + " undo, " + std::to_string(undo.redoCount()) + " redo" + (undo.canUndo() ? " | next undo: " + undo.undoLabel() : std::string()));
    }
  }

  std::unique_ptr<PageState> state_;
  WidgetId undoButton_, redoButton_, status_, panel_;
  props::UndoStack::ListenerId listener_ = 0;
};

}  // namespace

void buildGalleryProps(UiContext& ui, WidgetId parent) { ui.create<GalleryPropsPage>(parent); }

}  // namespace r1ui::widgets
