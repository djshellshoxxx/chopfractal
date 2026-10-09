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
endif()

juce_add_plugin(ChopFractal
  COMPANY_NAME "ChopFractal"
  PRODUCT_NAME "ChopFractal"
  PLUGIN_MANUFACTURER_CODE Cfrc
  PLUGIN_CODE Cfx1
  FORMATS VST3
  VST3_CATEGORIES Fx
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ChopFractal PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
target_include_directories(ChopFractal PRIVATE src)
target_compile_definitions(ChopFractal PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(ChopFractal
  PRIVATE
    chopfractal::composition
    chopfractal::plugin_ui_adapter
    chopfractal::plugin_host_adapter
    juce::juce_audio_utils
    juce::juce_audio_formats
  PUBLIC
    juce::juce_recommended_config_flags)

# Headless shell tests: no host and no window, but the real processor, parameter tree, state round trip,
# host-time translation and audio path.
if(CHOPFRACTAL_BUILD_TESTS)
  juce_add_console_app(ChopFractalShellTests PRODUCT_NAME "ChopFractalShellTests")
  target_sources(ChopFractalShellTests PRIVATE tests/test_plugin_shell.cpp src/PluginProcessor.cpp src/PluginEditor.cpp)
  target_include_directories(ChopFractalShellTests PRIVATE src ${CMAKE_CURRENT_LIST_DIR}/../modules/chop_contracts/tests)
  target_compile_definitions(ChopFractalShellTests PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(ChopFractalShellTests PRIVATE
    chopfractal::composition chopfractal::plugin_ui_adapter chopfractal::plugin_host_adapter
    juce::juce_audio_utils juce::juce_audio_formats juce::juce_recommended_config_flags)
  add_test(NAME plugin.shell COMMAND ChopFractalShellTests)
endif()
