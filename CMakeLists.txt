cmake_minimum_required(VERSION 3.20)
# VERSION (repo root) is the single source of truth: MAJOR.MINOR.PATCH with an optional -beta.N / -rc.N suffix.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CHOPFRACTAL_VERSION_FULL LIMIT_COUNT 1)
string(STRIP "${CHOPFRACTAL_VERSION_FULL}" CHOPFRACTAL_VERSION_FULL)
if(NOT CHOPFRACTAL_VERSION_FULL MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)(-(alpha|beta|rc)\\.[0-9]+)?$")
  message(FATAL_ERROR "VERSION must look like 1.2.3 or 1.2.3-beta.4, got '${CHOPFRACTAL_VERSION_FULL}'")
endif()
set(CHOPFRACTAL_VERSION_NUM "${CMAKE_MATCH_1}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
project(ChopFractal VERSION ${CHOPFRACTAL_VERSION_NUM} LANGUAGES CXX)
set(CHOPFRACTAL_BUILD_NUMBER "0" CACHE STRING "CI build number stamped into BuildInfo")

# The portable modules need 3.20; the JUCE plugin (juce_add_plugin, FetchContent options) needs 3.22.
if(CHOPFRACTAL_BUILD_PLUGIN AND CMAKE_VERSION VERSION_LESS 3.22)
  message(FATAL_ERROR "Building the plugin requires CMake 3.22 or newer (found ${CMAKE_VERSION}).")
endif()

option(CHOPFRACTAL_BUILD_TESTS "Build module and integration tests" ON)
option(CHOPFRACTAL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CHOPFRACTAL_SANITIZE "Build with AddressSanitizer + UBSan (GCC/Clang)" OFF)
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
          recursive_hit_zoom chop_roles_grammar wav_export midi_export evolve fractal_rhythm chop_insight preset_store time_stretch level_match
          audio_renderer plugin_host_adapter plugin_ui_adapter)
  add_subdirectory(modules/${m})
endforeach()
add_subdirectory(composition)

if(CHOPFRACTAL_BUILD_PLUGIN)
  enable_language(C)  # JUCE compiles a few C sources
  add_subdirectory(plugin)
endif()
