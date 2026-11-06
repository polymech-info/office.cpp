cmake_minimum_required(VERSION 3.20)

project(officecli-dotnet NONE)

find_program(DOTNET_EXECUTABLE dotnet REQUIRED)

set(OFFICECLI_SOLUTION "${CMAKE_CURRENT_SOURCE_DIR}/officecli.slnx")
set(OFFICECLI_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/src/officecli/officecli.csproj")
set(OFFICECLI_TEST_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/tests/OfficeCli.Tests/OfficeCli.Tests.csproj")

set(OFFICECLI_CONFIGURATION "Release" CACHE STRING "OfficeCLI .NET build configuration")
set(OFFICECLI_PUBLISH_RUNTIME "win-x64" CACHE STRING "Optional .NET runtime identifier for publish, e.g. win-x64, linux-x64, osx-arm64")
set(OFFICECLI_PUBLISH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../dist/win-x64/bin" CACHE PATH "OfficeCLI publish output directory")
set(OFFICECLI_PUBLISH_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/publish-stage" CACHE PATH "OfficeCLI staged publish output directory")
set(OFFICECLI_OFFLINE_ENV
  "${CMAKE_COMMAND}" -E env
  "OFFICECLI_NO_NETWORK=1"
  "OFFICECLI_SKIP_UPDATE=1"
  "OFFICECLI_NO_AUTO_INSTALL=1")

if(EXISTS "${OFFICECLI_TEST_PROJECT}")
  set(OFFICECLI_DEFAULT_BUILD_FILE "${OFFICECLI_SOLUTION}")
else()
  set(OFFICECLI_DEFAULT_BUILD_FILE "${OFFICECLI_PROJECT}")
  message(WARNING
    "officecli.slnx references tests/OfficeCli.Tests/OfficeCli.Tests.csproj, "
    "but that project is not present. The default officecli_build target will "
    "build src/officecli/officecli.csproj directly. Use officecli_slnx_build "
    "when the full solution tree is available.")
endif()

add_custom_target(officecli_restore
  COMMAND ${OFFICECLI_OFFLINE_ENV} "${DOTNET_EXECUTABLE}" restore "${OFFICECLI_DEFAULT_BUILD_FILE}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  USES_TERMINAL
  COMMENT "Restoring OfficeCLI .NET dependencies")

add_custom_target(officecli_build
  COMMAND ${OFFICECLI_OFFLINE_ENV} "${DOTNET_EXECUTABLE}" build "${OFFICECLI_DEFAULT_BUILD_FILE}" -c "${OFFICECLI_CONFIGURATION}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  USES_TERMINAL
  COMMENT "Building OfficeCLI with dotnet")

add_custom_target(officecli_slnx_build
  COMMAND ${OFFICECLI_OFFLINE_ENV} "${DOTNET_EXECUTABLE}" build "${OFFICECLI_SOLUTION}" -c "${OFFICECLI_CONFIGURATION}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  USES_TERMINAL
  COMMENT "Building OfficeCLI solution with dotnet")

if(EXISTS "${OFFICECLI_TEST_PROJECT}")
  add_custom_target(officecli_test
    COMMAND ${OFFICECLI_OFFLINE_ENV} "${DOTNET_EXECUTABLE}" test "${OFFICECLI_SOLUTION}" -c "${OFFICECLI_CONFIGURATION}" --no-build
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Testing OfficeCLI solution with dotnet")
else()
  add_custom_target(officecli_test
    COMMAND "${CMAKE_COMMAND}" -E echo "OfficeCLI test project is not present: ${OFFICECLI_TEST_PROJECT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Skipping OfficeCLI tests")
endif()

set(_officecli_publish_runtime_args "")
if(NOT OFFICECLI_PUBLISH_RUNTIME STREQUAL "")
  list(APPEND _officecli_publish_runtime_args -r "${OFFICECLI_PUBLISH_RUNTIME}")
endif()

add_custom_target(officecli_publish
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${OFFICECLI_PUBLISH_STAGE_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${OFFICECLI_PUBLISH_STAGE_DIR}"
  COMMAND ${OFFICECLI_OFFLINE_ENV} "${DOTNET_EXECUTABLE}" publish "${OFFICECLI_PROJECT}"
          -c "${OFFICECLI_CONFIGURATION}"
          -o "${OFFICECLI_PUBLISH_STAGE_DIR}"
          ${_officecli_publish_runtime_args}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${OFFICECLI_PUBLISH_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${OFFICECLI_PUBLISH_STAGE_DIR}" "${OFFICECLI_PUBLISH_DIR}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  USES_TERMINAL
  COMMENT "Publishing OfficeCLI with dotnet and copying to dist")
