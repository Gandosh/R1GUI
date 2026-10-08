# Audit tools registered as fast-tier tests. Per-batch exceptions go in allowlist.d/ (one file each).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME audit.license_boundary
           COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/license_boundary_audit.py)
  set_tests_properties(audit.license_boundary PROPERTIES LABELS "fast")
endif()
