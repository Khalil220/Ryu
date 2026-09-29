include(FetchContent)

FetchContent_Declare(prism
    URL https://github.com/ethindp/prism/releases/download/v0.18.2/prism-windows-x64.zip
    URL_HASH SHA256=31c02e3ef2260b4d3b11fb00132f8eb12bc147b5fb17031b580670b117ba7d23
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(prism)

add_library(prism::prism SHARED IMPORTED GLOBAL)
set_target_properties(prism::prism PROPERTIES
    IMPORTED_LOCATION "${prism_SOURCE_DIR}/dynamic/release/bin/prism.dll"
    IMPORTED_IMPLIB "${prism_SOURCE_DIR}/dynamic/release/lib/prism.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${prism_SOURCE_DIR}/include"
)
