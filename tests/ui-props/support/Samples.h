// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the sample host types the ui-props and property panel tests declare properties for: Transform
//   (position, rotation, scale), Material (colour, roughness, enum, an edit condition), Light (a
//   getter/setter pair, an enum, a unit), and Fragile (a setter that throws or refuses), each with its
//   PropertySet. Both the headless tests and the widget tests include this header.
// Why: a realistic range of member kinds (data members, getter/setter pairs, enums, strings, vectors,
//   colours) exercises every accessor and descriptor path with plain C++ types and no reflection.
// Callers: tests/ui-props/*_test.cpp, tests/ui-widgets/props/*_test.cpp.
#pragma once

#include <stdexcept>
#include <string>

#include "r1ui/props/PropertySet.h"

namespace samples {

using namespace r1ui::props;

// ---- Transform ------------------------------------------------------------------------------------------

struct Transform {
  Vec3 position{};
  Vec3 rotation{};
  Vec3 scale{1.0, 1.0, 1.0};
  bool visible = true;
  std::string name = "Object";
  int layer = 0;
};

inline const PropertySet<Transform>& transformSet() {
  static const PropertySet<Transform> set = [] {
    PropertySet<Transform> s("Transform");
    s.category("Transform", -10);
    s.add("position", "Position", &Transform::position).unit("m").units({{"cm", 0.01}, {"mm", 0.001}}).range(-1000.0, 1000.0).tooltip("World position");
    s.add("rotation", "Rotation", &Transform::rotation).unit("deg").range(-360.0, 360.0);
    s.add("scale", "Scale", &Transform::scale).range(0.001, 1000.0);
    s.category("Object");
    s.add("name", "Name", &Transform::name).maxLength(64);
    s.add("visible", "Visible", &Transform::visible);
    s.add("layer", "Layer", &Transform::layer).range(0, 31).advanced();
    s.defaultsFrom(Transform{});
    return s;
  }();
  return set;
}

// ---- Material -------------------------------------------------------------------------------------------

enum class Shading { Flat, Smooth, Toon };

struct Material {
  Color color{0.8f, 0.8f, 0.8f, 1.0f};
  double roughness = 0.5;
  Shading shading = Shading::Smooth;
  bool useTexture = false;
  std::string texture;
  double textureScale = 1.0;
  std::string name = "Material";
  std::string secret;
};

inline const PropertySet<Material>& materialSet() {
  static const PropertySet<Material> set = [] {
    PropertySet<Material> s("Material");
    s.category("Appearance");
    s.add("color", "Color", &Material::color);
    s.add("roughness", "Roughness", &Material::roughness).slider(0.0, 1.0).precision(2).tooltip("Surface roughness");
    s.add("shading", "Shading", &Material::shading).value("flat", Shading::Flat, "Flat").value("smooth", Shading::Smooth, "Smooth").value("toon", Shading::Toon, "Toon");
    s.category("Texture");
    s.add("useTexture", "Use texture", &Material::useTexture).asSwitch();
    s.add("texture", "Texture", &Material::texture).enableWhenTrue("useTexture");
    s.add("textureScale", "Texture scale", &Material::textureScale).range(0.01, 100.0).visibleWhenTrue("useTexture");
    s.category("Object");
    s.add("name", "Name", &Material::name);
    s.add("secret", "Secret", &Material::secret).password().advanced();
    s.defaultsFrom(Material{});
    return s;
  }();
  return set;
}

// ---- Light ----------------------------------------------------------------------------------------------

enum class LightUnit { Lumen, Candela, Lux };

class Light {
 public:
  double intensity() const { return intensity_; }
  void setIntensity(double v) {
    ++writes;
    intensity_ = v;
  }
  LightUnit unit() const { return unit_; }
  void setUnit(LightUnit u) { unit_ = u; }
  float radius() const { return radius_; }
  void setRadius(float r) { radius_ = r; }
  bool castShadows = true;
  std::string name = "Light";
  Color color{1.0f, 1.0f, 1.0f, 1.0f};
  int writes = 0;

 private:
  double intensity_ = 100.0;
  LightUnit unit_ = LightUnit::Lumen;
  float radius_ = 1.0f;
};

inline const PropertySet<Light>& lightSet() {
  static const PropertySet<Light> set = [] {
    PropertySet<Light> s("Light");
    s.category("Light");
    s.add("intensity", "Intensity", &Light::intensity, &Light::setIntensity).range(0.0, 1.0e6).softRange(0.0, 1000.0).step(10.0).unit("lm");
    s.add("unit", "Unit", &Light::unit, &Light::setUnit).value("lumen", LightUnit::Lumen, "Lumen").value("candela", LightUnit::Candela, "Candela").value("lux", LightUnit::Lux, "Lux");
    s.add("radius", "Radius", &Light::radius, &Light::setRadius).range(0.0, 100.0);
    s.add("castShadows", "Cast shadows", &Light::castShadows);
    s.add("color", "Color", &Light::color);
    s.category("Object");
    s.add("name", "Name", &Light::name);
    s.defaultsFrom(Light{});
    return s;
  }();
  return set;
}

// ---- Fragile (hostile host code) -----------------------------------------------------------------------

struct Fragile {
  double value = 0.0;
  double rejectAbove = 1000.0;
  static inline int throwCount = 0;
};

inline const PropertySet<Fragile>& fragileSet() {
  static const PropertySet<Fragile> set = [] {
    PropertySet<Fragile> s("Fragile");
    s.category("Fragile");
    s.add(
         "value", "Value", [](const Fragile& f) { return f.value; },
         [](Fragile& f, double v) {
           if (v > 100.0 && v <= 200.0 && f.rejectAbove >= 1000.0) {
             ++Fragile::throwCount;
             throw std::runtime_error("host failure");
           }
           if (v > f.rejectAbove) return false;  // this object refuses
           f.value = v;
           return true;
         })
        .defaultValue(0.0);
    return s;
  }();
  return set;
}

}  // namespace samples
