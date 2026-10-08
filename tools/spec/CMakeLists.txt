# Spec asset checks registered as fast-tier tests (no GPU, no browser needed).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME spec.imgdiff_selftest
           COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/imgdiff.py --selftest)
  add_test(NAME spec.assets_consistency
           COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/assets_check.py)
  set_tests_properties(spec.imgdiff_selftest spec.assets_consistency PROPERTIES LABELS "fast")
endif()
