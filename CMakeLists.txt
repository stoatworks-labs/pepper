cmake_minimum_required(VERSION 3.15)

project(Pepper
    VERSION 0.1.0
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Universal by default so one bundle loads in both the Apple Silicon and the
# Intel build of Resolume. Override with -DCMAKE_OSX_ARCHITECTURES=arm64 for a
# faster dev build.
#
# This must sit immediately after project() and before any target exists. CMake
# latches the architecture list when the first target is created; setting it
# later is silently ignored and you get an arm64-only binary that the build log
# calls a success. Verify with `lipo`, never with the log.
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple deprecated the whole OpenGL API in 10.14 and FFGL's plugin interface is
# still OpenGL, so every SDK call warns. There is no alternative API to move to
# here, so silencing it is not ignoring a problem.
if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

# windows.h defines min and max as macros, which turns every std::min into
# `std::(...)` and fails with an error naming neither min nor windows.h.
if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

option(PEPPER_BUILD_TOOLS "Build the offline render and measurement harness" ON)

# ---------------------------------------------------------------------------
# The Resolume FFGL SDK, as a git submodule pinned to b1afaf9 -- the rev the
# rest of the fleet pins.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ffgl/CMakeLists.txt")
    message(FATAL_ERROR
        "FFGL SDK not found. Run: git submodule update --init --recursive")
endif()

set(FFGL_BUILD_EXAMPLE_PLUGINS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/ffgl EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Everything both plugins are, as an OBJECT library so both bundles and the
# offline harness are built from exactly the same code.
#
# The registrations are NOT in here. Each lives in its bundle's own entry file
# (MixerEntry.cpp, FeedEntry.cpp): a registration in the shared library would
# put both plugins into both bundles. That is vectrix's two-bundle pattern.
# ---------------------------------------------------------------------------
add_library(pepper_core OBJECT
    source/StoatworksAbout.h
    source/StoatworksAboutLinks.h
    source/StoatworksAboutParams.h
    source/Diag.h
    source/Diag.cpp
    source/Timing.h
    source/Timing.cpp
    source/Names.h
    source/Controls.h
    source/Controls.cpp
    source/Rig.h
    source/Rig.cpp
    source/Shaders.h
    source/Shaders.cpp
    source/Pepper.h
    source/Pepper.cpp
    source/Feed.h
    source/Feed.cpp
)

target_include_directories(pepper_core PUBLIC source)
target_link_libraries(pepper_core PUBLIC ffgl::sdk)
target_compile_definitions(pepper_core PUBLIC PEPPER_VERSION="${PROJECT_VERSION}")
set_target_properties(pepper_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# On Windows/Linux the SDK headers pull in <GL/glew.h>. The SDK links GLEW
# privately, so consumers need it too -- for the include path and for the
# GLEW_STATIC define the static vcpkg triplet propagates. macOS uses the system
# OpenGL framework and needs none of this. GLEW arrives through vcpkg.json.
if(NOT APPLE)
    find_package(GLEW REQUIRED)
    target_link_libraries(pepper_core PUBLIC GLEW::GLEW)
endif()

# ---------------------------------------------------------------------------
# The plugins. FFGL resolves one plugMain per binary, so a mixer and an effect
# are two bundles -- not one bundle with two entries.
# ---------------------------------------------------------------------------
function(pepper_add_plugin target entry bundle_name identifier)
    add_library(${target} MODULE ${entry})
    target_link_libraries(${target} PRIVATE pepper_core)

    if(APPLE)
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION "bundle"
            OUTPUT_NAME "${bundle_name}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${bundle_name}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "${identifier}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        )
        # FFGL hosts resolve plugMain by symbol name, so keep default visibility.
        target_link_libraries(${target} PRIVATE
            "-framework OpenGL"
            "-framework Carbon"
            "-framework AppKit"
        )
    else()
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${bundle_name}")
    endif()
endfunction()

pepper_add_plugin(Pepper source/MixerEntry.cpp
    "Pepper" "com.stoatworks.ffgl.pepper")
pepper_add_plugin(PepperFeed source/FeedEntry.cpp
    "PepperFeed" "com.stoatworks.ffgl.pepperfeed")

# `cmake --install build` drops both bundles where Arena reads them. Avenue
# reads ~/Documents/Resolume Avenue/Extra Effects instead; pass --prefix.
#
# A mixer goes in Extra Effects like every other FFGL plugin: measured on
# genlock in Resolume Arena 7.27.1 (2026-09-23), Arena scans Extra Effects,
# loads a mixer from there and offers it as a layer Blend Mode.
if(APPLE AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/Documents/Resolume Arena/Extra Effects"
        CACHE PATH "" FORCE)
endif()
install(TARGETS Pepper PepperFeed
    LIBRARY DESTINATION "."
    BUNDLE  DESTINATION "."
)

# ---------------------------------------------------------------------------
# The harness: the real plugin classes in a real GL context, with no host.
#
# macOS only, because it stands up a CGL context directly. It feeds the mixer
# TWO inputs and the feed one -- see tools/pgtest/main.cpp. It links the same
# OBJECT library the bundles do, so it measures the code that ships, and
# neither entry file, so it registers nothing.
# ---------------------------------------------------------------------------
if(PEPPER_BUILD_TOOLS AND APPLE)
    enable_testing()

    add_executable(pgtest tools/pgtest/main.cpp)
    # zlib ships with the OS, which is why the PNG writer is fifty lines
    # rather than a vendored dependency.
    target_link_libraries(pgtest PRIVATE pepper_core z
        "-framework OpenGL"
        "-framework AppKit"
    )

    foreach(check names mixer additive energy fresnel double geometry ripple opacity pyramid feed mutation)
        add_test(NAME ${check} COMMAND pgtest --${check})
    endforeach()
endif()
