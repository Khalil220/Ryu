add_library(ryu_core STATIC
    encoding.cpp
    hls.cpp
    host_repair.cpp
    html.cpp
    http.cpp
    language.cpp
    log.cpp
    playlist_server.cpp
    registry.cpp
    providers/anizone.cpp
    providers/hianime.cpp
    providers/kickassanime.cpp
    resolvers/megaplay.cpp
    stream_finder.cpp
    update.cpp
)
target_include_directories(ryu_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ryu_core
    PUBLIC CURL::libcurl
    PRIVATE nlohmann_json::nlohmann_json ${RYU_LEXBOR}
)
if(WIN32)
    target_link_libraries(ryu_core PRIVATE bcrypt ws2_32)
endif()
