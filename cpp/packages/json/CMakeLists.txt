include(FetchContent)

# RapidJSON — use master for CMake 4.x compatibility (v1.1.0 is from 2016)
FetchContent_Declare(
    rapidjson
    GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

set(RAPIDJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_GetProperties(rapidjson)
if(NOT rapidjson_POPULATED)
    FetchContent_Populate(rapidjson)
    # Don't add_subdirectory — just use the headers
endif()

add_library(json STATIC
    src/json.cpp
)

target_include_directories(json
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC ${rapidjson_SOURCE_DIR}/include
)
