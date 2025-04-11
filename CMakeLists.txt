cmake_minimum_required(VERSION 3.25.1)
project(cmus VERSION 1.0.0)

# Define INSTALL_PREFIX and PROJECT_VERSION as cache variables
set(INSTALL_PREFIX "" CACHE STRING "The installation prefix")
set(PROJECT_VERSION ${PROJECT_VERSION} CACHE STRING "The project version")

# Set default INSTALL_PREFIX if not specified
if(NOT INSTALL_PREFIX)
    set(INSTALL_PREFIX "/usr/local")
endif()

# Set the CMAKE_INSTALL_PREFIX to the INSTALL_PREFIX variable
set(CMAKE_INSTALL_PREFIX ${INSTALL_PREFIX})

set(COMPILER_INFO "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Compiler Info: ${COMPILER_INFO}")

configure_file(
    ${PROJECT_SOURCE_DIR}/src/include/config.h.in
    ${PROJECT_SOURCE_DIR}/src/include/config.h
    @ONLY
)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/src/include)

# Source files
file(GLOB_RECURSE SOURCES
    src/*.c
    src/main.c
)

# Add executable
add_executable(cmus ${SOURCES})

# Configure a header file to pass INSTALL_PREFIX and PROJECT_VERSION
configure_file(
    ${PROJECT_SOURCE_DIR}/src/include/config.h.in
    ${PROJECT_SOURCE_DIR}/src/include/config.h
    @ONLY
)

# Install targets
install(TARGETS cmus DESTINATION bin)

# Uninstall target
if(NOT TARGET uninstall)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()

