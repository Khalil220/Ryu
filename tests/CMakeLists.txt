find_package(doctest CONFIG REQUIRED)

add_library(ryu_test_main OBJECT test_main.cpp)
target_link_libraries(ryu_test_main PUBLIC doctest::doctest)

add_executable(ryu_unit_tests
    unit/test_encoding.cpp
    unit/test_format.cpp
    unit/test_html.cpp
    unit/test_kickassanime.cpp
    unit/test_language.cpp
    unit/test_log.cpp
    unit/test_hianime.cpp
    unit/test_hls.cpp
    unit/test_host_repair.cpp
    unit/test_megaplay.cpp
    unit/test_registry.cpp
    unit/test_settings.cpp
    unit/test_stall_watch.cpp
    unit/test_stream_finder.cpp
    unit/test_subtitle_choice.cpp
)
target_link_libraries(ryu_unit_tests PRIVATE ryu_core ryu_ui_support ryu_test_main)
target_compile_definitions(ryu_unit_tests PRIVATE RYU_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME unit COMMAND ryu_unit_tests)
set_tests_properties(unit PROPERTIES LABELS unit)

add_executable(ryu_live_tests
    live/test_live_fallback.cpp
    live/test_live_hianime.cpp
    live/test_live_kickassanime.cpp
    live/test_live_repair.cpp
)
target_link_libraries(ryu_live_tests PRIVATE ryu_core ryu_test_main)
add_test(NAME live COMMAND ryu_live_tests)
set_tests_properties(live PROPERTIES LABELS live)

set(RYU_UI_TEST_PYTHON "" CACHE FILEPATH "Python interpreter with pywinauto installed; enables the ui test")
if(RYU_UI_TEST_PYTHON)
    add_test(NAME ui COMMAND "${RYU_UI_TEST_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/ui/test_ui.py" $<TARGET_FILE:ryu>)
    set_tests_properties(ui PROPERTIES LABELS ui TIMEOUT 300)
    add_test(NAME ui-responsiveness
             COMMAND "${RYU_UI_TEST_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/ui/test_responsiveness.py" $<TARGET_FILE:ryu>)
    set_tests_properties(ui-responsiveness PROPERTIES LABELS ui TIMEOUT 180)
endif()
