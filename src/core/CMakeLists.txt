add_library(ryu_core STATIC
    encoding.cpp
    html.cpp
    http.cpp
    registry.cpp
    providers/hianime.cpp
)
target_include_directories(ryu_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ryu_core
    PUBLIC CURL::libcurl
    PRIVATE nlohmann_json::nlohmann_json ${RYU_LEXBOR}
)
