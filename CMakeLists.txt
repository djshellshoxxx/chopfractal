cmake_minimum_required(VERSION 3.20)
project(ChopFractal VERSION 0.1.0 LANGUAGES CXX)

option(CHOPFRACTAL_BUILD_TESTS "Build module and integration tests" ON)
option(CHOPFRACTAL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CHOPFRACTAL_SANITIZE "Build with AddressSanitizer + UBSan (GCC/Clang)" OFF)
option(CHOPFRACTAL_BUILD_PLUGIN "Build the JUCE VST3 shell (requires JUCE)" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CHOPFRACTAL_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

if(CHOPFRACTAL_BUILD_TESTS)
  enable_testing()
endif()

# Dependency order: contracts first, generic modules, domain modules, features, renderer, adapters.
foreach(m chop_contracts state_codec variation_history source_chop pattern_engine
          recursive_hit_zoom chop_roles_grammar audio_renderer)
  add_subdirectory(modules/${m})
endforeach()

if(CHOPFRACTAL_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()
