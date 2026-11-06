# macOS-specific build settings for mono-cpp CLI
# This is optional - the main CLI builds fine on macOS with the root CMakeLists.txt
# This file can be used for macOS-specific extensions if needed in the future

if(NOT APPLE)
  return()
endif()

# macOS-specific settings for the main CLI target if needed
# Currently the mono-cpp CLI builds with standard CMake toolchain on macOS
message(STATUS "Building on macOS - mono-cpp CLI")
