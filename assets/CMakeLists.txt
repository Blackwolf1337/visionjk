#============================================================================
# The assets of the JA+ port that are not code: menus, shaders, effects,
# models, sabers, strings. Every directory named at the end of this file is packed
# into a pk3 when the tree is built (it lands in <build>/assets) and is
# installed into the mod directory.
#
# A directory <name> has a list <name>.order next to it: the order of the
# entries of the pk3, see make_pk3.py for why that matters.
#============================================================================

set(JPAssetsDir "japlus" CACHE STRING "The mod directory the asset archives of assets/ are installed into")

# Python packs them: cmake -E tar can not write the directory entries the engine needs
if(CMAKE_VERSION VERSION_LESS 3.12)
	find_program(JPAssetsPython NAMES python3 python)
else()
	find_package(Python3 COMPONENTS Interpreter QUIET)
	set(JPAssetsPython "${Python3_EXECUTABLE}")
endif()
if(NOT JPAssetsPython)
	message(WARNING "No Python 3 found: the asset archives of assets/ are not built. -DBuildJPAssets=OFF silences this.")
	return()
endif()

# jp_add_pk3(<name of the pk3 without .pk3> <directory in assets/>)
function(jp_add_pk3 name directory)
	set(source "${CMAKE_CURRENT_SOURCE_DIR}/${directory}")
	set(order "${CMAKE_CURRENT_SOURCE_DIR}/${directory}.order")
	set(packer "${CMAKE_CURRENT_SOURCE_DIR}/make_pk3.py")
	set(output "${CMAKE_CURRENT_BINARY_DIR}/${name}.pk3")

	# a file that was added or removed is seen at the next build (from CMake 3.12 on, before that: run cmake again)
	if(CMAKE_VERSION VERSION_LESS 3.12)
		file(GLOB_RECURSE files "${source}/*")
	else()
		file(GLOB_RECURSE files CONFIGURE_DEPENDS "${source}/*")
	endif()

	add_custom_command(OUTPUT "${output}"
		COMMAND "${JPAssetsPython}" "${packer}" "${source}" "${order}" "${output}"
		DEPENDS ${files} "${order}" "${packer}"
		COMMENT "Packing ${name}.pk3"
		VERBATIM)
	add_custom_target(${name} ALL DEPENDS "${output}")

	# next to where the modules of the mod are looked for, see codemp/ui/CMakeLists.txt
	if(MakeApplicationBundles AND BuildMPEngine)
		install(FILES "${output}"
			DESTINATION "${JKAInstallDir}/${MPEngine}.app/Contents/MacOS/${JPAssetsDir}"
			COMPONENT ${JKAMPCoreComponent})
	else()
		install(FILES "${output}"
			DESTINATION "${JKAInstallDir}/${JPAssetsDir}"
			COMPONENT ${JKAMPCoreComponent})
	endif()
endfunction()

# One archive with all of it: the contents of clientPlugin_v1.4B4.pk3, japlus_sabers.pk3 and efx_MOD.pk3 of the
# JA+ 2.4 Build 7 package, with the changes of the port (four menus in ui/jamp). Left out: the thumbnail caches
# of Windows Explorer (Thumbs.db) and the two modules of the plugin, cgamex86.dll and uix86.dll, which the
# engine would unpack and load in place of the modules of this tree.
jp_add_pk3(zz_japlus_client_assets japlus_client)
