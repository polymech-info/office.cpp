cmake_minimum_required(VERSION 3.20)

project(pm-office
    VERSION 0.1.0
    DESCRIPTION "pm-office OOXML create and edit CLI"
    LANGUAGES CXX
)

# compile_commands.json for clangd / IDE
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output all executables to dist/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/dist")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist")

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: UTF-8 source/execution charset
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Optional: compile-time log level control (0=debug, 1=info, 2=warn, 3=error)
option(MONO_CPP_LOG_LEVEL "Compile-time log level (0-3, default 0=debug)" "0")

# Dependencies
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(cli11 tomlplusplus)

# minizip-ng: ZIP container reader for OOXML packages (.xlsx/.docx/.pptx).
# Keep it static because the office package is shipped as a reusable static lib;
# the CLI is only a development consumer.
set(MZ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MZ_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(MZ_BUILD_FUZZ_TESTS OFF CACHE BOOL "" FORCE)
set(MZ_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MZ_FETCH_LIBS ON CACHE BOOL "" FORCE)
set(MZ_ZLIB ON CACHE BOOL "" FORCE)
set(MZ_BZIP2 OFF CACHE BOOL "" FORCE)
set(MZ_LZMA OFF CACHE BOOL "" FORCE)
set(MZ_ZSTD OFF CACHE BOOL "" FORCE)
set(MZ_PKCRYPT OFF CACHE BOOL "" FORCE)
set(MZ_WZAES OFF CACHE BOOL "" FORCE)
set(MZ_OPENSSL OFF CACHE BOOL "" FORCE)
set(MZ_LIBCOMP OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
set(ZLIB_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ZLIBNG_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    minizip_ng
    GIT_REPOSITORY https://github.com/zlib-ng/minizip-ng.git
    GIT_TAG        4.0.8
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(minizip_ng)

FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        v1.14
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(pugixml)

# Mustache - header-only template engine
FetchContent_Declare(
    kainjow_mustache
    GIT_REPOSITORY https://github.com/kainjow/Mustache.git
    GIT_TAG        v4.1
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(kainjow_mustache)
if(NOT kainjow_mustache_POPULATED)
    FetchContent_Populate(kainjow_mustache)
endif()

# dotenv-cpp - load .env files
FetchContent_Declare(
    laserpants_dotenv
    GIT_REPOSITORY https://github.com/laserpants/dotenv-cpp.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(laserpants_dotenv)
if(NOT laserpants_dotenv_POPULATED)
    FetchContent_Populate(laserpants_dotenv)
endif()
add_library(laserpants_dotenv INTERFACE)
target_include_directories(laserpants_dotenv INTERFACE ${laserpants_dotenv_SOURCE_DIR}/include)
add_library(laserpants::dotenv ALIAS laserpants_dotenv)

# glob - file globbing (** patterns)
FetchContent_Declare(
    pranav_glob
    GIT_REPOSITORY https://github.com/p-ranav/glob.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(pranav_glob)
if(NOT pranav_glob_POPULATED)
    FetchContent_Populate(pranav_glob)
endif()
add_library(glob STATIC ${pranav_glob_SOURCE_DIR}/source/glob.cpp)
target_include_directories(glob PUBLIC ${pranav_glob_SOURCE_DIR}/include)
target_compile_features(glob PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(glob PRIVATE /permissive-)
endif()

# Packages
add_subdirectory(packages/logger)
add_subdirectory(packages/json)
add_subdirectory(packages/html)
add_subdirectory(packages/ipc)

# Reusable Office static library. The CLI target below is intentionally only a
# development consumer; production embedding should link this library directly.
add_library(office STATIC
    src/lib/office/document_node.cpp
    src/lib/office/office_edit.cpp
    src/lib/office/office_markdown.cpp
    src/lib/office/office_parser.cpp
    src/lib/office/ooxml_metadata.cpp
    src/lib/office/ooxml_zip.cpp
    src/lib/office/word_resolver.cpp
    src/lib/office/xlsx_package.cpp
    src/lib/office/xlsx_resolver.cpp
    src/lib/office/xlsx_workbook.cpp
)

target_include_directories(office PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/office
)
target_include_directories(office PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../commons.cpp/src
)
target_compile_definitions(office PRIVATE _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING)

target_compile_features(office PUBLIC cxx_std_20)

target_link_libraries(office PUBLIC
    minizip
    pugixml::pugixml
)

if(MSVC)
    target_compile_options(office PRIVATE /W4 /permissive-)
else()
    target_compile_options(office PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Main executable
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/lib/office/main_office_cli.cpp
    src/sys_metrics.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    CLI11::CLI11
    tomlplusplus::tomlplusplus
    logger
    office
    json
    html
    ipc
    glob
    laserpants::dotenv
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${kainjow_mustache_SOURCE_DIR}
)

# Compile-time log level
target_compile_definitions(${PROJECT_NAME} PRIVATE
    MONO_CPP_LOG_LEVEL=${MONO_CPP_LOG_LEVEL}
)

# Compiler warnings
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Install
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
