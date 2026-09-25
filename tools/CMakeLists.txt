add_executable(ryu_cli ryu_cli.cpp)
set_target_properties(ryu_cli PROPERTIES OUTPUT_NAME ryu-cli)
target_link_libraries(ryu_cli PRIVATE ryu_core)
