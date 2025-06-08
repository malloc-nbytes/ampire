cmake_minimum_required(VERSION 3.25.1)
project(ampire VERSION 0.1.4)

# Define INSTALL_PREFIX and PROJECT_VERSION as cache variables
set(INSTALL_PREFIX "" CACHE STRING "The installation prefix")
set(PROJECT_VERSION ${PROJECT_VERSION} CACHE STRING "The project version")
set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "Enable SDL Unix console build" FORCE)

# Set default INSTALL_PREFIX if not specified
if(NOT INSTALL_PREFIX)
    set(INSTALL_PREFIX "/usr/local")
endif()

# Set the CMAKE_INSTALL_PREFIX to the INSTALL_PREFIX variable
set(CMAKE_INSTALL_PREFIX ${INSTALL_PREFIX})

set(COMPILER_INFO "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "Compiler Info: ${COMPILER_INFO}")

# Configure a header file to pass INSTALL_PREFIX and PROJECT_VERSION
configure_file(
    ${PROJECT_SOURCE_DIR}/src/include/config.h.in
    ${PROJECT_SOURCE_DIR}/src/include/config.h
    @ONLY
)

# Include directories for your project
include_directories(${PROJECT_SOURCE_DIR}/src/include)

# Add SDL3 submodule
add_subdirectory(external/SDL3)

# Ensure SDL3 is built as a shared library
set(SDL3_SHARED ON CACHE BOOL "Build SDL3 as a shared library" FORCE)
set(SDL3_STATIC OFF CACHE BOOL "Build SDL3 as a static library" FORCE)

set(SDLMIXER_VENDORED ON CACHE BOOL "Use vendored libraries for SDL3_mixer" FORCE)
set(SDLMIXER_VORBIS_STB ON CACHE BOOL "Enable OGG Vorbis support with stb_vorbis" FORCE)
set(SDLMIXER_VORBIS_VORBIS OFF CACHE BOOL "Disable libvorbis support to avoid conflicts" FORCE)
set(SDLMIXER_VORBIS_TREMOR OFF CACHE BOOL "Disable Tremor support to avoid conflicts" FORCE)
set(SDLMIXER_OGG ON CACHE BOOL "Enable OGG music support" FORCE)

# Add SDL3_mixer submodule
add_subdirectory(external/SDL3_mixer)

# Install SDL3 shared library
install(TARGETS SDL3-shared
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install SDL3_mixer shared library
install(TARGETS SDL3_mixer-shared
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Ensure SDL3_mixer is built as a shared library
set(SDL3MIXER_SHARED ON CACHE BOOL "Build SDL3_mixer as a shared library" FORCE)
set(SDL3MIXER_STATIC OFF CACHE BOOL "Build SDL3_mixer as a static library" FORCE)

# Source files
file(GLOB_RECURSE SOURCES
    src/*.c
    src/main.c
)

# Add executable
add_executable(ampire ${SOURCES})

# Link SDL3 and SDL3_mixer to the executable
target_link_libraries(ampire PRIVATE SDL3::SDL3-shared SDL3_mixer::SDL3_mixer-shared ncurses tinfo)

# Install targets
install(TARGETS ampire DESTINATION bin)

# Uninstall target
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()
