// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: the visual oracle of the toast against the default and error toast captures in both themes:
//   box, tone colour, 12 px icon, text, and for the error toast the copy and close buttons. The
//   reference toasts sit on the ruler strip of the editor window, which this scene does not have, so
//   only the toast rectangle itself is compared.
// Callers: CTest (label gpu, offscreen, no window).
// Not covered: the warning tone (no reference capture exists; its colours come from the warning
//   tokens and are checked by the unit test of the style rows only).
#include "../menu/ScreenCompare.h"
#include "r1ui/widgets/toast/Toast.h"

namespace {

using namespace r1test::screen;
using r1ui::theme::ThemeId;

BuildFn showToast(std::string text, ToastTone tone) {
  return [=](UiContext& ui, WidgetId) {
    // The manager handle only needs to live while the toast is shown; the toast outlives it.
    ToastManager toasts(ui);
    ToastSpec spec;
    spec.text = text;
    spec.tone = tone;
    const ToastId id = toasts.show(spec);
    ui.frame();
    return toasts.widgetOf(id);
  };
}

}  // namespace

int main() {
  for (const ThemeId theme : {ThemeId::Dark, ThemeId::Light}) {
    Case def;
    def.reference = "widget-toast-default";
    def.clipX = 642.86;
    def.clipY = 2;
    def.theme = theme;
    def.profile = "text";
    def.luminance = true;  // reference text has LCD subpixel antialiasing; ours is grayscale
    def.page = false;
    def.only = {{6, 6, 142, 28}};
    R1_EXPECT_CROP(showToast("Copied as node ID", ToastTone::Default), def);

    Case err = def;
    err.reference = "widget-toast-error";
    err.clipX = 568.44;
    err.only = {{6, 6, 291, 30}};
    R1_EXPECT_CROP(showToast("Failed to open file: unexpected token", ToastTone::Error), err);
  }
  return r1test::finish();
}
