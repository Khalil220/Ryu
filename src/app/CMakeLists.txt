add_library(ryu_ui_support STATIC
    format.cpp
    settings.cpp
    stall_watch.cpp
    subtitle_choice.cpp
)
target_include_directories(ryu_ui_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ryu_ui_support PUBLIC ryu_core wx::base)

add_executable(ryu WIN32
    accessibility.cpp
    app.cpp
    main_frame.cpp
    player_panel.cpp
    preferences_dialog.cpp
    show_details_panel.cpp
    speech.cpp
    text_list.cpp
    ryu.rc
    ryu.manifest
)
target_link_libraries(ryu PRIVATE ryu_ui_support wx::core wx::base libmpv prism::prism oleacc)
add_custom_command(TARGET ryu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:ryu> $<TARGET_FILE_DIR:ryu>
    COMMAND_EXPAND_LISTS
)
