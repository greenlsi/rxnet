cmake_minimum_required(VERSION 3.15)
project(rxnet
    VERSION 0.1.0
    DESCRIPTION "Synchronous runtime for reactive models (FSM and Petri Nets)"
    HOMEPAGE_URL "https://github.com/greenlsi/rxnet"
    LANGUAGES C
)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Compilation flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_POSIX_C_SOURCE=200809L")

option(RX_TRACE_ENABLE "Enable tracing support in rxnet" OFF)
option(BUILD_TESTS "Build test suite" OFF)

if(BUILD_TESTS AND NOT RX_TRACE_ENABLE)
    message(STATUS "BUILD_TESTS=ON requires tracing; enabling RX_TRACE_ENABLE")
    set(RX_TRACE_ENABLE ON CACHE BOOL "Enable tracing support in rxnet" FORCE)
endif()

# ==============================================================================
# Main Library: rxnet
# ==============================================================================

add_library(rxnet STATIC
    src/runtime.c
    src/fsm.c
    src/pn.c
    src/cyclic.c
    src/coop.c
    src/thread.c
)

if(RX_TRACE_ENABLE)
    target_sources(rxnet PRIVATE src/trace.c)
endif()

# Public headers
target_include_directories(rxnet
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Compile definitions for trace (optional)
if(RX_TRACE_ENABLE)
    target_compile_definitions(rxnet PUBLIC RX_TRACE_ENABLE)
endif()

if(BUILD_TESTS)
    enable_testing()

    add_executable(test_runtime tests/test_runtime.c)
    target_link_libraries(test_runtime PRIVATE rxnet)
    add_test(NAME test_runtime COMMAND test_runtime)

    add_executable(test_fsm tests/test_fsm.c)
    target_link_libraries(test_fsm PRIVATE rxnet)
    add_test(NAME test_fsm COMMAND test_fsm)

    add_executable(test_pn tests/test_pn.c)
    target_link_libraries(test_pn PRIVATE rxnet)
    add_test(NAME test_pn COMMAND test_pn)

    add_executable(test_trace tests/test_trace.c)
    target_link_libraries(test_trace PRIVATE rxnet)
    add_test(NAME test_trace COMMAND test_trace)
endif()

# ==============================================================================
# Installation
# ==============================================================================

# Install library
install(TARGETS rxnet
    EXPORT rxnetTargets
    ARCHIVE DESTINATION lib
)

# Install public headers
install(DIRECTORY include/rxnet
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Install CMake config files for find_package()
install(EXPORT rxnetTargets
    FILE rxnetTargets.cmake
    NAMESPACE rxnet::
    DESTINATION lib/cmake/rxnet
)

# Generate and install version file
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/rxnetConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Generate and install config file
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/rxnetConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/rxnetConfig.cmake"
    INSTALL_DESTINATION lib/cmake/rxnet
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/rxnetConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/rxnetConfigVersion.cmake"
    DESTINATION lib/cmake/rxnet
)

# ==============================================================================
# Package for CPack
# ==============================================================================

set(CPACK_PACKAGE_VENDOR "GreenLSI")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
include(CPack)

# ==============================================================================
# Summary
# ==============================================================================

message(STATUS "")
message(STATUS "  rxnet ${PROJECT_VERSION}")
message(STATUS "  ======================================")
message(STATUS "  Build Type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  Trace Enabled:     ${RX_TRACE_ENABLE}")
message(STATUS "  Build Tests:       ${BUILD_TESTS}")
message(STATUS "  Install Prefix:    ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
