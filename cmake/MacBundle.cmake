#============================================================================
# Makes the app the build puts together run on its own (macOS only, run as a step of the build):
#
# - the libraries the engine links from Homebrew (SDL2, which is sdl2-compat) are copied into
#   Contents/Frameworks and the engine is pointed there
# - sdl2-compat loads SDL3 when it runs, from next to itself: SDL3 goes beside it
# - at the end everything is signed ad hoc. install_name_tool breaks the signature the linker made, and a
#   program without a valid one is ended by macOS on Apple Silicon. A signed app also gets its own entry in
#   the privacy settings (microphone for the voice chat)
#
# Called with -DAPP=<path of the .app> -DENGINE=<name of the program in Contents/MacOS>
# and optionally -DSDL3=<libSDL3 dylib>
#============================================================================

set(contents "${APP}/Contents")
set(engine "${contents}/MacOS/${ENGINE}")
set(frameworks "${contents}/Frameworks")
file(MAKE_DIRECTORY "${frameworks}")

function(bundle_copy from name)
	get_filename_component(real "${from}" REALPATH)
	execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${real}" "${frameworks}/${name}")
	execute_process(COMMAND chmod u+w "${frameworks}/${name}")
	execute_process(COMMAND install_name_tool -id "@executable_path/../Frameworks/${name}" "${frameworks}/${name}"
		ERROR_QUIET)
endfunction()

# the libraries of Homebrew (or /usr/local) the engine is linked against
execute_process(COMMAND otool -L "${engine}" OUTPUT_VARIABLE deps)
string(REGEX MATCHALL "/(opt/homebrew|usr/local)/[^ \t\n]+\\.dylib" deps "${deps}")
foreach(dep ${deps})
	get_filename_component(name "${dep}" NAME)
	bundle_copy("${dep}" "${name}")
	execute_process(COMMAND install_name_tool -change "${dep}" "@executable_path/../Frameworks/${name}" "${engine}")
endforeach()

if(SDL3 AND EXISTS "${SDL3}")
	bundle_copy("${SDL3}" "libSDL3.dylib")
endif()

# sign from the inside out: the libraries and modules first, then the app
file(GLOB_RECURSE parts "${frameworks}/*.dylib" "${contents}/MacOS/*.dylib")
foreach(part ${parts})
	execute_process(COMMAND codesign --force --sign - "${part}" OUTPUT_QUIET ERROR_QUIET)
endforeach()
execute_process(COMMAND codesign --force --sign - "${APP}" RESULT_VARIABLE signed OUTPUT_QUIET ERROR_VARIABLE err)
if(NOT signed EQUAL 0)
	message(WARNING "codesign of ${APP} failed: ${err}")
endif()
