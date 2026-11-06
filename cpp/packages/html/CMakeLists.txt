include(FetchContent)

FetchContent_Declare(
    lexbor
    GIT_REPOSITORY https://github.com/lexbor/lexbor.git
    GIT_TAG        v2.4.0
    GIT_SHALLOW    TRUE
)

# Build lexbor as static
set(LEXBOR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_STATIC ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lexbor)

add_library(html STATIC
    src/html.cpp
    src/html2md.cpp
    src/table.cpp
)

# MSVC: treat source and execution charset as UTF-8
# (fixes \u200b zero-width-space mismatch in html2md tests)
if(MSVC)
    target_compile_options(html PRIVATE /utf-8)
endif()

target_include_directories(html
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(html
    PUBLIC lexbor_static
)
