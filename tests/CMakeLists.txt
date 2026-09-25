find_package(doctest CONFIG REQUIRED)

add_library(ryu_test_main OBJECT test_main.cpp)
target_link_libraries(ryu_test_main PUBLIC doctest::doctest)

add_executable(ryu_unit_tests
    unit/test_encoding.cpp
    unit/test_html.cpp
    unit/test_hianime.cpp
    unit/test_registry.cpp
)
target_link_libraries(ryu_unit_tests PRIVATE ryu_core ryu_test_main)
target_compile_definitions(ryu_unit_tests PRIVATE RYU_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME unit COMMAND ryu_unit_tests)
set_tests_properties(unit PROPERTIES LABELS unit)

add_executable(ryu_live_tests
    live/test_live_hianime.cpp
)
target_link_libraries(ryu_live_tests PRIVATE ryu_core ryu_test_main)
add_test(NAME live COMMAND ryu_live_tests)
set_tests_properties(live PROPERTIES LABELS live)
