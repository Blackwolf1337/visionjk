# The client for the browser: engine, cgame, ui and rd-vulkan-vision as WebAssembly, built with Emscripten.
#
#   source <emsdk>/emsdk_env.sh
#   cmake -S . -B buildwasm -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/wasm32-emscripten.cmake -DCMAKE_BUILD_TYPE=Release
#   cmake --build buildwasm
#
# Built and tried with Emscripten 6.0.12. The loader of the modules (shared/sys/web/sys_web.js) calls
# loadWebAssemblyModule() of Emscripten's libdylink.js, which is no public interface: with another version
# check that file first.

if(NOT DEFINED ENV{EMSDK})
	message(FATAL_ERROR "EMSDK is not set: source emsdk_env.sh of the Emscripten SDK first")
endif()
include("$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")

# what a browser can not run or does not need
set(BuildMPDed OFF CACHE BOOL "" FORCE)
set(BuildMPGame OFF CACHE BOOL "" FORCE)
set(BuildMPRdVanilla OFF CACHE BOOL "" FORCE)
set(BuildJPAssets OFF CACHE BOOL "" FORCE)

# Off unless asked for with -D: Steam Audio is being brought to the browser.
set(BuildJPSpatialAudio OFF CACHE BOOL "")

# there are no system libraries to find
set(UseInternalZlib ON CACHE BOOL "" FORCE)
set(UseInternalPNG ON CACHE BOOL "" FORCE)
set(UseInternalJPEG ON CACHE BOOL "" FORCE)
