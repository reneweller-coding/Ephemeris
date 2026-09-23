# Ephemeris -- a generator for long Berlin-school pieces and concerts (docs/PLAN.md).
#
# Layout:
#   Core/         framework-free engine (static library EphemerisCore)
#   Tools/render  eph_render: offline render, MIDI export
#   Tests/        eph_selftest and the vector-path variants of eph_vectest
#   Plugin/       JUCE VST3 + Standalone (Phase 5)
#   Quest/        native OpenXR app (Phase 6)
#
# Built after Phosphene's root CMakeLists.txt at 9a2f615; the rules below are its rules.
cmake_minimum_required(VERSION 3.22)
project(Ephemeris VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# One source of truth for the version: this project() line (a lesson of Noctuary 1.1.0).
add_compile_definitions(EPH_VERSION="${PROJECT_VERSION}")

option(EPH_BUILD_TOOLS "Build eph_render and the tests" ON)
option(EPH_AVX2 "Build the x86-64 core with AVX2 + FMA" ON)
option(EPH_STATIC_RUNTIME "Link the MSVC runtime statically (for distribution)" OFF)

if(MSVC)
    add_compile_options(/MP)
endif()
if(EPH_STATIC_RUNTIME AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# No fast-math and no floating-point contraction: the offline render is the determinism oracle and the
# lane paths (Vec.h) are tested bit for bit against the scalar path. Clang contracts a*b+c by default;
# Intel's icx does even under /fp:precise (measured in Phosphene, 23.09.2026).
if(NOT MSVC)
    add_compile_options(-ffp-contract=off)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
    add_compile_options(/fp:precise /Qfma- /clang:-Wno-overriding-option)
endif()
if(ANDROID)
    set(EPH_AVX2 OFF CACHE BOOL "" FORCE)
endif()

add_subdirectory(Core)
if(EPH_BUILD_TOOLS)
    enable_testing()
    add_subdirectory(Tools/render)
    add_subdirectory(Tests)
endif()
