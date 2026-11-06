include(FetchContent)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

add_library(logger STATIC
    src/logger.cpp
)

target_include_directories(logger
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(logger
    PUBLIC spdlog::spdlog
)
