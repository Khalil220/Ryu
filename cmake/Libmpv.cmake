include(FetchContent)

FetchContent_Declare(libmpv
    URL https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/20260925/mpv-dev-x86_64-20260925-git-2a4eb8067c.7z
    URL_HASH SHA256=bae4b8275f4db6ec5a6bb0c0e5d497c305a2aa9fb065097a0d86bb7b86895c7d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(libmpv)

set(RYU_LIBMPV_IMPLIB "${libmpv_BINARY_DIR}/mpv.lib")
if(NOT EXISTS "${RYU_LIBMPV_IMPLIB}")
    get_filename_component(msvc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(RYU_DUMPBIN dumpbin HINTS "${msvc_bin}" REQUIRED)
    find_program(RYU_LIB_TOOL lib HINTS "${msvc_bin}" REQUIRED)
    execute_process(
        COMMAND "${RYU_DUMPBIN}" /nologo /exports "${libmpv_SOURCE_DIR}/libmpv-2.dll"
        OUTPUT_VARIABLE libmpv_exports
        COMMAND_ERROR_IS_FATAL ANY
    )
    string(REGEX MATCHALL "mpv_[A-Za-z0-9_]+" libmpv_symbols "${libmpv_exports}")
    list(REMOVE_DUPLICATES libmpv_symbols)
    list(JOIN libmpv_symbols "\n" libmpv_def_body)
    file(WRITE "${libmpv_BINARY_DIR}/mpv.def" "LIBRARY libmpv-2.dll\nEXPORTS\n${libmpv_def_body}\n")
    execute_process(
        COMMAND "${RYU_LIB_TOOL}" /nologo "/def:${libmpv_BINARY_DIR}/mpv.def" /machine:x64 "/out:${RYU_LIBMPV_IMPLIB}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()

add_library(libmpv SHARED IMPORTED GLOBAL)
set_target_properties(libmpv PROPERTIES
    IMPORTED_LOCATION "${libmpv_SOURCE_DIR}/libmpv-2.dll"
    IMPORTED_IMPLIB "${RYU_LIBMPV_IMPLIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${libmpv_SOURCE_DIR}/include"
)
