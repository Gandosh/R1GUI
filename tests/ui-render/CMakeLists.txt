# Tests for ui-render. Registered by this module's own file (merge-friendly).
#   fast tier: painter, clip stack, SDF reference and instance packing; no GPU, no window.
#   gpu tier:  real shader output through OffscreenTarget readback, WindowTarget lifecycle and the
#              validation layer (Debug trees). Label `gpu`, deliberately not `fast`.
add_executable(ui-render-paint-test painter_test.cpp)
target_link_libraries(ui-render-paint-test PRIVATE r1ui::render_paint r1ui_options)
add_test(NAME ui-render.paint COMMAND ui-render-paint-test)
set_tests_properties(ui-render.paint PROPERTIES LABELS "fast")

if(TARGET ui-render)
  add_executable(ui-render-gpu-test gpu_test.cpp)
  target_link_libraries(ui-render-gpu-test PRIVATE r1ui::render r1ui_options)
  # One CTest entry per case; the program rejects names it does not know.
  foreach(case
      device_selection exact_fill rounded_aa border_width shadow_blur clip_correctness alpha_blend
      textured_quad texture_lifetime batching_stats hostile_inputs panel_reference
      window_lifecycle multi_window frame_cost timings_and_memory)
    add_test(NAME ui-render.gpu.${case} COMMAND ui-render-gpu-test ${case})
    set_tests_properties(ui-render.gpu.${case} PROPERTIES LABELS "gpu" TIMEOUT 300)
  endforeach()
endif()
