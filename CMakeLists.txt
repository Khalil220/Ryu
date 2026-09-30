cmake_minimum_required(VERSION 3.28)
project(Ryu VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
endif()

find_package(CURL CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(lexbor CONFIG REQUIRED)
find_package(wxWidgets CONFIG REQUIRED)
include(cmake/Libmpv.cmake)
include(cmake/Prism.cmake)

if(TARGET lexbor::lexbor_static)
    set(RYU_LEXBOR lexbor::lexbor_static)
else()
    set(RYU_LEXBOR lexbor::lexbor)
endif()

add_subdirectory(src/core)
add_subdirectory(src/app)
add_subdirectory(tools)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
