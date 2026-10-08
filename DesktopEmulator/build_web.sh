#!/bin/bash
# Minimal Emscripten build for the Vircon32 web emulator.
# Bypasses the desktop CMakeLists.txt entirely (that one pulls in ImGui,
# glad, PNG, and other desktop-only dependencies) and instead compiles just
# the files a bare-bones web build actually needs: the platform-independent
# console logic, a handful of infrastructure files, and a purpose-written
# MainWeb.cpp instead of Main.cpp+GUI.cpp+Settings.cpp+Languages.cpp.
set -e

cd "$(dirname "$0")"

CONSOLELOGIC_SRC=$(ls ConsoleLogic/*.cpp)
INFRA_SRC="DesktopInfrastructure/FilePaths.cpp DesktopInfrastructure/Logger.cpp DesktopInfrastructure/StringFunctions.cpp"
EMULATOR_SRC="Emulator/AudioOutput.cpp Emulator/EmulatorControl.cpp Emulator/GamepadsInput.cpp Emulator/Globals.cpp Emulator/StopWatch.cpp Emulator/VideoOutput.cpp Emulator/MainWeb.cpp"

mkdir -p WebBuild

em++ \
    -std=c++11 -O2 \
    -I. -IEmulator -IConsoleLogic -IDesktopInfrastructure \
    -s USE_SDL=2 \
    -s WASM=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s MIN_WEBGL_VERSION=1 \
    -s MAX_WEBGL_VERSION=1 \
    -s EXIT_RUNTIME=0 \
    -s DISABLE_EXCEPTION_CATCHING=0 \
    -s FORCE_FILESYSTEM=1 \
    -s GL_ASSERTIONS=0 \
    -lidbfs.js \
    --pre-js Emulator/preload_cartridge.js \
    --pre-js Emulator/persist_storage.js \
    --shell-file Emulator/shell.html \
    --preload-file Data/Bios/StandardBios.v32@/data/Bios/StandardBios.v32 \
    $CONSOLELOGIC_SRC $INFRA_SRC $EMULATOR_SRC \
    -o WebBuild/Vircon32Web.html

echo "Build finished: WebBuild/Vircon32Web.html"
echo "Copy a ROM named game.v32 into WebBuild/ next to the .html file"
echo "(or use ?rom=yourname.v32 in the page URL) - it is now fetched at"
echo "runtime instead of being baked into the build."
