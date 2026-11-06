add_library(ipc STATIC
    src/ipc.cpp
)

target_include_directories(ipc
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ipc
    PUBLIC json logger
)
