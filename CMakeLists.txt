cmake_minimum_required(VERSION 3.20)
# VERSION is the single source of the product version (module.json versions are API versions and are separate).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CF_VERSION_FULL LIMIT_COUNT 1)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" CF_VERSION_NUM "${CF_VERSION_FULL}")
project(ChopFractal VERSION ${CF_VERSION_NUM} LANGUAGES CXX)

# The portable modules need 3.20; the JUCE plugin (juce_add_plugin, FetchContent options) needs 3.22.
if(CHOPFRACTAL_BUILD_PLUGIN AND CMAKE_VERSION VERSION_LESS 3.22)
  message(FATAL_ERROR "Building the plugin requires CMake 3.22 or newer (found ${CMAKE_VERSION}).")
endif()

option(CHOPFRACTAL_BUILD_TESTS "Build module and integration tests" ON)
option(CHOPFRACTAL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CHOPFRACTAL_SANITIZE "Build with AddressSanitizer + UBSan (GCC/Clang)" OFF)
option(CHOPFRACTAL_RELEASE "Build a release: enforces the JUCE license decision" OFF)
set(CHOPFRACTAL_JUCE_LICENSE "UNSET" CACHE STRING "JUCE license route: UNSET | AGPL | COMMERCIAL")
set(CHOPFRACTAL_BUILD_NUMBER "0" CACHE STRING "CI build number stamped into the plugin")
if(CHOPFRACTAL_RELEASE)
  if(CHOPFRACTAL_JUCE_LICENSE STREQUAL "UNSET")
    message(FATAL_ERROR "Release builds need a JUCE license decision. Set -DCHOPFRACTAL_JUCE_LICENSE=COMMERCIAL (you hold a JUCE commercial license) or AGPL (the whole plugin is published under AGPLv3). See docs/LICENSING.md.")
  elseif(CHOPFRACTAL_JUCE_LICENSE STREQUAL "AGPL")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE_ID" CF_LICENSE_ID)
    if(NOT CF_LICENSE_ID MATCHES "AGPL-3.0")
      message(FATAL_ERROR "AGPL release requires LICENSE_ID=AGPL-3.0-or-later; run tools/set_license.py")
    endif()
  elseif(NOT CHOPFRACTAL_JUCE_LICENSE STREQUAL "COMMERCIAL")
    message(FATAL_ERROR "CHOPFRACTAL_JUCE_LICENSE must be UNSET, AGPL or COMMERCIAL.")
  endif()
endif()
option(CHOPFRACTAL_BUILD_PLUGIN "Build the JUCE VST3 shell (requires JUCE)" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Bit-exact results across compilers and CPUs: forbid fused multiply-add contraction (clang on arm64 enables it by
# default), which would change the last bit of generation math and the saved doubles. MSVC does not contract by default.
if(NOT MSVC)
  add_compile_options(-ffp-contract=off)
endif()

if(CHOPFRACTAL_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

if(CHOPFRACTAL_BUILD_TESTS)
  enable_testing()
endif()

# Dependency order: contracts first, generic modules, domain modules, features, renderer, adapters.
foreach(m chop_contracts state_codec variation_history source_chop pattern_engine
          recursive_hit_zoom chop_roles_grammar wav_export midi_export evolve scenes time_stretch fractal_rhythm chop_insight preset_store input_capture
          audio_renderer plugin_host_adapter plugin_ui_adapter)
  add_subdirectory(modules/${m})
endforeach()
add_subdirectory(composition)

if(CHOPFRACTAL_BUILD_PLUGIN)
  enable_language(C)  # JUCE compiles a few C sources
  add_subdirectory(plugin)
endif()
