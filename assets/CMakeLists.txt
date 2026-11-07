#============================================================================
# Copyright (C) 2026, VisionJK contributors
#
# This file is part of the VisionJK source code.
#
# VisionJK is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#============================================================================

#============================================================================
# The assets of the JA+ port that are not code: menus, shaders, effects,
# models, sabers, strings. Every directory named at the end of this file is packed
# into a pk3 when the tree is built (it lands in <build>/assets) and is
# installed into the mod directory. The configs are installed loose.
#
# A directory <name> has a list <name>.order next to it: the order of the
# entries of the pk3, see make_pk3.py for why that matters.
#============================================================================

# next to the modules of the mod (JPAssetsDir and JPModInstallDir are set in the CMakeLists.txt of the tree), on
# macOS next to the app as well, not in it
set(JPAssetsInstallDir "${JPModInstallDir}")

# jp_add_configs(<directory in assets/>): its .cfg files are installed into the mod directory as they are, not
# packed, and only where there is no file of that name yet: they are for the admin of a server to edit.
function(jp_add_configs directory)
	install(CODE "
		file(GLOB configs \"${CMAKE_CURRENT_SOURCE_DIR}/${directory}/*.cfg\")
		foreach(config \${configs})
			get_filename_component(name \"\${config}\" NAME)
			if(EXISTS \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${JPAssetsInstallDir}/\${name}\")
				message(STATUS \"Kept: \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${JPAssetsInstallDir}/\${name}\")
			else()
				file(INSTALL \"\${config}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${JPAssetsInstallDir}\")
			endif()
		endforeach()"
		COMPONENT ${JKAMPCoreComponent})
endfunction()

# The configs of the JA+ 2.4 Build 7 package: the example of a server config (server.cfg with admin.cfg, jk2.cfg,
# vstr.cfg, chat.cfg, saber.cfg), the scales of the player models, the blocking walls of four levels and the
# texts of the auto replier. As they came, but for server.cfg: the passwords it set for the three admin ranks are
# empty here, the ones of the package can be looked up by everybody, and its rcon password is gone with rcon.
jp_add_configs(configs)

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
	# A [ in an element keeps the ; after it from splitting the list until a ] (the emoji #`[ and #`] of gfx/emoji,
	# names of TaystJK), and the build then wants one file named "...#`[.png;...#`].png". Taken out of the
	# dependencies with string(), which knows no lists; the packer still packs them, a change to only such a file is
	# packed with the next change of another one.
	string(REGEX REPLACE "[^;]*[][][^;]*(;|$)" "" files "${files}")

	add_custom_command(OUTPUT "${output}"
		COMMAND "${JPAssetsPython}" "${packer}" "${source}" "${order}" "${output}"
		DEPENDS ${files} "${order}" "${packer}"
		COMMENT "Packing ${name}.pk3"
		VERBATIM)
	add_custom_target(${name} ALL DEPENDS "${output}")

	install(FILES "${output}"
		DESTINATION "${JPAssetsInstallDir}"
		COMPONENT ${JKAMPCoreComponent})
endfunction()

# One archive with all of it: the contents of clientPlugin_v1.4B4.pk3, japlus_sabers.pk3, efx_MOD.pk3 and
# japlus_gla_anims.pk3 of the JA+ 2.4 Build 7 package, with the changes of the port (four menus in ui/jamp). Left out: the thumbnail caches
# of Windows Explorer (Thumbs.db) and the two modules of the plugin, cgamex86.dll and uix86.dll, which the
# engine would unpack and load in place of the modules of this tree.
# Added to it: the voice lines of Jedi Outcast that Jedi Academy does not have, from its assets0 to
# assets5.pk3, in its three languages: English (sound/chars, 1733 files), German (sound/chr_d) and French
# (sound/chr_f). A line the game or this directory had under the same name, with either ending, was left as it
# is. Their folders are named at the end of japlus_client.order.
jp_add_pk3(zz_japlus_client_assets japlus_client)
