cmake_minimum_required(VERSION 3.22)
# The JUCE VST3 shell. Optional (-DCHOPFRACTAL_BUILD_PLUGIN=ON); everything it calls is the portable,
# already-tested composition root and adapters, so this directory holds only host/GUI glue.
#
#   -DCHOPFRACTAL_JUCE_DIR=/path/to/JUCE   use a local checkout instead of fetching
#   -DCHOPFRACTAL_JUCE_TAG=8.0.15          JUCE release to fetch (default pinned below)
#
# Licensing: JUCE is AGPLv3 / commercial and bundles the VST3 SDK. ChopFractal itself is all rights reserved
# for now; a closed-source release needs a JUCE commercial license. See docs/LICENSING.md before distributing.
include(FetchContent)
set(CHOPFRACTAL_JUCE_TAG "8.0.15" CACHE STRING "JUCE release tag to fetch")
if(CHOPFRACTAL_JUCE_DIR)
  add_subdirectory("${CHOPFRACTAL_JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce")
else()
  FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG ${CHOPFRACTAL_JUCE_TAG} GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(juce)
  # Tags can be moved: fail loudly if the fetched commit is not the one this project was tested with.
  set(CHOPFRACTAL_JUCE_COMMIT "91ad83ae34a81e0833b1a2b0866f54846370ae53" CACHE STRING "Expected JUCE commit for CHOPFRACTAL_JUCE_TAG (empty disables the check)")
  if(CHOPFRACTAL_JUCE_COMMIT AND CHOPFRACTAL_JUCE_TAG STREQUAL "8.0.15")
    find_package(Git QUIET)
    if(GIT_FOUND)
      execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${juce_SOURCE_DIR}" rev-parse HEAD OUTPUT_VARIABLE _juce_head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      if(_juce_head AND NOT _juce_head STREQUAL CHOPFRACTAL_JUCE_COMMIT)
        message(FATAL_ERROR "JUCE ${CHOPFRACTAL_JUCE_TAG} resolved to ${_juce_head}, expected ${CHOPFRACTAL_JUCE_COMMIT}. The tag moved or the cache is stale.")
      endif()
    endif()
  endif()
endif()

# Optional stand-alone application (own window and audio device selector, no DAW needed). Produces ChopFractal_Standalone.
option(CHOPFRACTAL_BUILD_STANDALONE "Also build a stand-alone application" OFF)
set(CHOPFRACTAL_FORMATS VST3)
if(CHOPFRACTAL_BUILD_STANDALONE)
  list(APPEND CHOPFRACTAL_FORMATS Standalone)
  # Dismiss the notice without changing JUCE's feedback-loop input mute protection.
  # The patch is version-checked and idempotent for cached fetched checkouts.
  if(NOT CHOPFRACTAL_JUCE_DIR)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../tools/patch_standalone_banner.py"
              "${juce_SOURCE_DIR}/modules/juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h"
      RESULT_VARIABLE _cf_banner_result)
    if(NOT _cf_banner_result EQUAL 0)
      message(FATAL_ERROR "Could not add the standalone notification dismiss button")
    endif()
  else()
    message(STATUS "Local JUCE checkout: standalone notification patch not applied")
  endif()
endif()

juce_add_plugin(ChopFractal
  COMPANY_NAME "ChopFractal"
  PRODUCT_NAME "ChopFractal"
  VERSION ${PROJECT_VERSION}
  PLUGIN_MANUFACTURER_CODE Cfrc
  PLUGIN_CODE Cfx1
  FORMATS ${CHOPFRACTAL_FORMATS}
  VST3_CATEGORIES Fx
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE)

# Optional CLAP format via clap-juce-extensions (MIT; wraps the same JUCE processor, no source changes).
# Enable with -DCHOPFRACTAL_BUILD_CLAP=ON; produces target ChopFractal_CLAP.
option(CHOPFRACTAL_BUILD_CLAP "Also build a CLAP plugin" OFF)
if(CHOPFRACTAL_BUILD_CLAP)
  # Pinned to a commit: release 0.26.0 predates JUCE 8.0.11+ (it still includes juce_audio_processors/.../juce_LegacyAudioParameter.cpp).
  set(CHOPFRACTAL_CLAP_EXT_TAG "7adee3a1bd4684d4caa5601100e364abccff4b4d" CACHE STRING "clap-juce-extensions commit to fetch")
  if(CHOPFRACTAL_CLAP_EXT_DIR)
    add_subdirectory("${CHOPFRACTAL_CLAP_EXT_DIR}" "${CMAKE_BINARY_DIR}/clap-juce-extensions" EXCLUDE_FROM_ALL)
  else()
    FetchContent_Declare(clap_juce_extensions GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
                         GIT_TAG ${CHOPFRACTAL_CLAP_EXT_TAG} GIT_SHALLOW FALSE GIT_SUBMODULES_RECURSE TRUE)
    FetchContent_MakeAvailable(clap_juce_extensions)
  endif()
endif()

# The plugin's own sources, shared by the plugin and the shell tests. A new panel or editor class (src/*Panel.cpp,
# src/Editor*.cpp) and a new test (tests/test_*.cpp) is picked up on the next configure without editing this file.
file(GLOB CHOPFRACTAL_PLUGIN_SOURCES CONFIGURE_DEPENDS
  src/PluginProcessor.cpp src/PluginEditor.cpp src/PresetManager.cpp src/DragOut.cpp
  src/Editor*.cpp src/*Panel.cpp src/StatusBar.cpp)
file(GLOB CHOPFRACTAL_SHELL_TEST_SOURCES CONFIGURE_DEPENDS tests/test_*.cpp)
target_sources(ChopFractal PRIVATE ${CHOPFRACTAL_PLUGIN_SOURCES})
# Version stamp generated from the root VERSION file (single source of truth).
configure_file(src/BuildInfo.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.h" @ONLY)
target_include_directories(ChopFractal PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(ChopFractal PRIVATE src)
# Warnings for our own plugin sources only (JUCE's module sources are compiled into the same target and stay untouched).
set(CHOPFRACTAL_WARNING_SOURCES ${CHOPFRACTAL_PLUGIN_SOURCES} ${CHOPFRACTAL_SHELL_TEST_SOURCES})
if(MSVC)
  set(_cf_warn /W4 $<$<BOOL:${CHOPFRACTAL_WARNINGS_AS_ERRORS}>:/WX>)
else()
  set(_cf_warn -Wall -Wextra $<$<BOOL:${CHOPFRACTAL_WARNINGS_AS_ERRORS}>:-Werror>)
endif()
set_source_files_properties(${CHOPFRACTAL_WARNING_SOURCES} PROPERTIES COMPILE_OPTIONS "${_cf_warn}")
target_compile_definitions(ChopFractal PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(ChopFractal
  PRIVATE
    chopfractal::composition
    chopfractal::plugin_ui_adapter
    chopfractal::preset_store
    chopfractal::plugin_host_adapter
    chopfractal::input_capture
    juce::juce_audio_utils
    juce::juce_audio_formats
  PUBLIC
    juce::juce_recommended_config_flags)

if(CHOPFRACTAL_BUILD_CLAP)
  clap_juce_extensions_plugin(TARGET ChopFractal
    CLAP_ID "com.chopfractal.chopfractal"
    CLAP_FEATURES audio-effect utility)
endif()

# Headless shell tests: no host and no window, but the real processor, parameter tree, state round trip,
# host-time translation and audio path.
if(CHOPFRACTAL_BUILD_TESTS)
  juce_add_console_app(ChopFractalShellTests PRODUCT_NAME "ChopFractalShellTests")
  target_sources(ChopFractalShellTests PRIVATE ${CHOPFRACTAL_SHELL_TEST_SOURCES} ${CHOPFRACTAL_PLUGIN_SOURCES})
  target_include_directories(ChopFractalShellTests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated" ${CMAKE_CURRENT_LIST_DIR}/../modules/chop_contracts/tests)
  target_compile_definitions(ChopFractalShellTests PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
    CHOPFRACTAL_VERSION_FILE="${CMAKE_CURRENT_LIST_DIR}/../VERSION")
  target_link_libraries(ChopFractalShellTests PRIVATE
    chopfractal::composition chopfractal::plugin_ui_adapter chopfractal::preset_store chopfractal::plugin_host_adapter chopfractal::input_capture
    juce::juce_audio_utils juce::juce_audio_formats juce::juce_recommended_config_flags)
  add_test(NAME plugin.shell COMMAND ChopFractalShellTests)
endif()
