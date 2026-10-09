// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the pixel-equality oracle of the bound bars outside edit mode: in one render of the customization
//   gallery page the toolbar and the menu bar the customization layer builds (CustomizableToolbar and
//   CustomizableMenuBar over a customization-free layout) are compared pixel by pixel with a Toolbar and a
//   MenuBar built directly with bindCommandToolbar and bindCommandMenuBar from the same commands, in both
//   themes; the File menu opened from each bar is compared too (its panel crop). Any difference fails.
//   After a customization (hide, rename, size step) the bars must differ from the direct ones, proving the
//   comparison is not vacuous.
// Callers: CTest (label gpu, offscreen, no window).
#include <memory>

#include "NoDialogs.h"
#include "VisualSupport.h"
#include "r1ui/widgets/commands/CommandMenus.h"
#include "r1ui/widgets/commands/CommandToolbar.h"
#include "r1ui/widgets/customize/GalleryCustomize.h"
#include "r1ui/widgets/menu/MenuPanel.h"

namespace {

using namespace r1ui::widgets;
using namespace r1ui::widgets::testing;
using r1ui::core::tree::WidgetId;
namespace layout = r1ui::core::layout;

struct Rects {
  layout::Rect customToolbar, directToolbar, customBar, directBar, menuPanel;
};

enum class Scene { Closed, CustomMenuOpen, DirectMenuOpen, Customized };

BuildFn sceneIn(Scene scene, std::shared_ptr<Rects> out) {
  return [scene, out](UiContext& ui, WidgetId parent) {
    ui.rootStyle().direction = layout::FlexDirection::Column;
    ui.rootStyle().gapRow = 70.0;  // far apart: the shadow of one bar never reaches another
    for (double& p : ui.rootStyle().padding) p = 40.0;
    // The gallery page only provides the commands and the controller; its own widgets are not shown.
    GalleryCustomizePage& page = createGalleryCustomize(ui, parent);
    page.style().display = layout::Display::None;
    CustomizableMenuBar& customBar = ui.create<CustomizableMenuBar>(parent, page.controller());
    CustomizableToolbar& customToolbar = ui.create<CustomizableToolbar>(parent, page.controller(), "tb.tools");
    // The direct references: the same commands bound without any customization layer.
    Toolbar& toolbar = ui.create<Toolbar>(parent);
    using I = CommandToolbarItem;
    auto binding = bindCommandToolbar(ui, toolbar, page.sync(), {I::command("tool.select"), I::command("tool.pen"), I::command("tool.text"), I::command("tool.hand"), I::separator(),
                                                                   I::group({"tool.rect", "tool.ellipse"}), I::separator(), I::command("edit.undo"), I::command("edit.redo")});
    MenuBar& bar = ui.create<MenuBar>(parent);
    bar.style().alignSelf = layout::Align::Start;
    using E = CommandMenuEntry;
    bindCommandMenuBar(bar, page.services(),
                       {{"File", {E::command("file.open"), E::command("file.save"), E::separator(), E::command("file.export")}},
                        {"Edit", {E::command("edit.undo"), E::command("edit.redo"), E::separator(), E::command("edit.cut"), E::command("edit.copy"), E::command("edit.paste"),
                                  E::separator(), E::heading("Selection"), E::command("edit.duplicate"), E::command("edit.delete"), E::command("edit.selectAll")}},
                        {"View", {E::command("view.grid"), E::command("view.rulers"), E::separator(),
                                  E::submenu("Zoom", {E::command("view.zoomIn"), E::command("view.zoomOut"), E::command("view.fit")})}},
                        {"Tools", {E::command("tool.select"), E::command("tool.pen"), E::command("tool.text"), E::command("tool.hand")}},
                        {"Help", {E::command("help.docs"), E::command("help.about")}}});
    // Keep the binding alive with the toolbar: park it in a holder widget owned by the scene.
    class Keep final : public WidgetObject {
     public:
      const char* typeName() const override { return "Keep"; }
      void onAttached() override { style().display = layout::Display::None; }
      std::unique_ptr<CommandToolbarBinding> binding;
    };
    ui.create<Keep>(parent).binding = std::move(binding);
    ui.frame();
    if (scene == Scene::Customized) {
      page.model().hideEntry("tb.tools.tool.pen");
      page.model().setToolbarSizeStep("tb.tools", r1ui::commands::customize::SizeStep::Large);
      page.model().renameLabel("menu.edit", "Editing");
      ui.frame();
    }
    out->customToolbar = ui.absRect(customToolbar.childWidget());
    out->directToolbar = ui.absRect(toolbar.id());
    out->customBar = ui.absRect(customBar.childWidget());
    out->directBar = ui.absRect(bar.id());
    if (scene == Scene::CustomMenuOpen || scene == Scene::DirectMenuOpen) {
      MenuBar* target = scene == Scene::CustomMenuOpen ? customBar.menuBar() : &bar;
      target->openMenu(0);
      ui.frame();
      out->menuPanel = ui.absRect(target->controller().panelAt(0));
    }
    return parent;
  };
}

// The pixels of a crop; the rectangles must be on the image.
std::vector<uint8_t> crop(const image::Image& img, const layout::Rect& r, int margin) {
  std::vector<uint8_t> out;
  for (int y = r.y - margin; y < r.y + r.h + margin; ++y) {
    for (int x = r.x - margin; x < r.x + r.w + margin; ++x) {
      for (int c = 0; c < 4; ++c) out.push_back(img.rgba[(static_cast<size_t>(y) * img.width + static_cast<size_t>(x)) * 4 + static_cast<size_t>(c)]);
    }
  }
  return out;
}

}  // namespace

int main() {
  const VisualPaths paths = r1test::visual::paths();
  for (const r1ui::theme::ThemeId theme : {r1ui::theme::ThemeId::Dark, r1ui::theme::ThemeId::Light}) {
    const char* name = theme == r1ui::theme::ThemeId::Dark ? "dark" : "light";
    RenderSpec spec;
    spec.width = 1200;
    spec.height = 1000;
    spec.theme = theme;
    spec.padding = 0;
    spec.background = "panel";
    const auto render = [&](Scene scene, std::shared_ptr<Rects>& rects) {
      rects = std::make_shared<Rects>();
      return renderWidget(sceneIn(scene, rects), spec, paths);
    };
    std::shared_ptr<Rects> r0, r1, r2, r3;
    const image::Image closed = render(Scene::Closed, r0);
    const image::Image customOpen = render(Scene::CustomMenuOpen, r1);
    const image::Image directOpen = render(Scene::DirectMenuOpen, r2);
    const image::Image customized = render(Scene::Customized, r3);
    image::writePng(paths.artifactDir / (std::string("bars-closed-") + name + ".png"), closed.width, closed.height, closed.rgba);

    // The toolbar the customization layer builds is the toolbar the command binder builds: same pixels.
    R1_EXPECT(r0->customToolbar.w == r0->directToolbar.w && r0->customToolbar.h == r0->directToolbar.h && r0->customToolbar.w > 100);
    const bool toolbarSame = crop(closed, r0->customToolbar, 0) == crop(closed, r0->directToolbar, 0);
    std::printf("bars %-5s toolbar pixel-equal: %s (%dx%d)\n", name, toolbarSame ? "yes" : "NO", r0->customToolbar.w, r0->customToolbar.h);
    R1_EXPECT(toolbarSame);
    // The menu bar likewise.
    R1_EXPECT(r0->customBar.w == r0->directBar.w && r0->customBar.h == r0->directBar.h && r0->customBar.w > 100);
    const bool barSame = crop(closed, r0->customBar, 0) == crop(closed, r0->directBar, 0);
    std::printf("bars %-5s menu bar pixel-equal: %s (%dx%d)\n", name, barSame ? "yes" : "NO", r0->customBar.w, r0->customBar.h);
    R1_EXPECT(barSame);
    // The File menu opened from each: same panel pixels.
    R1_EXPECT(r1->menuPanel.w == r2->menuPanel.w && r1->menuPanel.h == r2->menuPanel.h && r1->menuPanel.w > 100);
    const bool menuSame = crop(customOpen, r1->menuPanel, 0) == crop(directOpen, r2->menuPanel, 0);
    std::printf("bars %-5s open menu pixel-equal: %s (%dx%d)\n", name, menuSame ? "yes" : "NO", r1->menuPanel.w, r1->menuPanel.h);
    R1_EXPECT(menuSame);
    // After a customization the bars differ from the references (the comparison is not vacuous).
    R1_EXPECT(r3->customToolbar.w != r3->directToolbar.w || r3->customToolbar.h != r3->directToolbar.h);
    R1_EXPECT(crop(customized, r3->customBar, 0) != crop(customized, r3->directBar, 0));
  }
  R1_EXPECT(validationMessageCount() == 0);
  return r1test::finish();
}
