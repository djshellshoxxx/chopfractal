#!/usr/bin/env bash
# Builds Steinberg's official VST3 validator (SDK 3.8.0, the version JUCE 8 reports) in a cache directory and
# runs it on the built plugin bundle. This is the spec's "clean VST3 validation" release gate.
#
#   cmake -S . -B build/plugin -G Ninja -DCHOPFRACTAL_BUILD_PLUGIN=ON -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/plugin --target ChopFractal_VST3
#   tools/validate_vst3.sh build/plugin/plugin/ChopFractal_artefacts/Release/VST3/ChopFractal.vst3
#
# Linux needs: libgtk-3-dev libgtkmm-3.0-dev (the SDK's configure step asks for them; the validator itself does not use GTK).
set -euo pipefail
bundle="${1:?usage: tools/validate_vst3.sh <path/to/ChopFractal.vst3>}"
cache="${CHOPFRACTAL_VST3_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/chopfractal-vst3sdk}"
tag="${CHOPFRACTAL_VST3_TAG:-v3.8.0_build_66}"
if [ ! -x "$cache/build/bin/Release/validator" ]; then
  rm -rf "$cache"
  git clone -q --depth 1 --branch "$tag" https://github.com/steinbergmedia/vst3sdk.git "$cache"
  git -C "$cache" submodule update -q --init --depth 1 base pluginterfaces public.sdk cmake
  cmake -S "$cache" -B "$cache/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF \
        -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=ON -DSMTG_CREATE_PLUGIN_LINK=OFF \
        -DSMTG_RUN_VST_VALIDATOR=OFF >/dev/null
  cmake --build "$cache/build" --target validator >/dev/null
fi
"$cache/build/bin/Release/validator" "$bundle"
