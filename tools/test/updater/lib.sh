#!/bin/bash
# What the tests of the updater share: where things are, the two test programs, the folder of a player and the
# release a server has. Sourced by e2e.sh, and by wintest.sh and winfs.sh of the agent (a Windows VM), not run.
# The paths come from ../test-env.sh.
#
# WIN=1 in the environment: the Windows build (buildwin64) and not the one for Linux (build64).
# UPD_BUILD: another build folder than those, for a look at how an older build does (visionjk/build-win is the
# one of the last release).
# A script can set these before it sources this file: UPD_ROOT (the folder everything is made in, default
# build-test/updater), UPD_PORT (8099), UPD_URL (where the test programs ask, default the loopback address
# with that port), UPD_PROG (the name of the program for Windows, default jptest.exe).
# shellcheck disable=SC2034

# shellcheck source=tools/test/test-env.sh
. "$(dirname "${BASH_SOURCE[0]}")/../test-env.sh"
src=$GAME_REPO
# the folder of the original game, whose base assets the test game links and whose client assets it copies
gamedata=$(dirname "$BASE_ASSETS")
R=${UPD_ROOT:-$WORK/updater}
port=${UPD_PORT:-8099}
if [ -n "$WIN" ]; then
	b=${UPD_BUILD:-$src/buildwin64}
	sys=windows
	prog=${UPD_PROG:-jptest.exe}
	G=$R/wingame
	H=$R/winhome
	X=$R/winexe
	mods=("$b/uix86_64.dll" "$b/cgamex86_64.dll" "$b/jampgamex86_64.dll")
	libs=("$b/rd-vanilla_x86_64.dll" "$src/lib/SDL2/bin/x64/SDL2.dll" /usr/x86_64-w64-mingw32/bin/libwinpthread-1.dll)
	export WINEPREFIX=$R/wine WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=-all
else
	b=${UPD_BUILD:-$src/build64}
	sys=linux
	prog=visionjk.x86_64
	G=$R/game
	H=$R/home
	X=$R/exe
	mods=("$b/codemp/ui/uix86_64.so" "$b/codemp/cgame/cgamex86_64.so" "$b/codemp/game/jampgamex86_64.so")
	libs=("$b/codemp/rd-vanilla/rd-vanilla_x86_64.so")
fi
url=${UPD_URL:-http://127.0.0.1:$port/update}

# The modules as a release has them: stripped (build-game.sh installs with --strip). Above UPDATE_QUICK_BYTES
# (16 MB) the look at every start takes a file by its size alone, and the modules of a build folder with their
# symbols are bigger than that on Windows (the game module 17 MB on 2026-10-24, 5.8 MB stripped): a module of
# the same size and other bytes would go unseen, and the test of the repair would test another case than a
# player has.
strip_mods() {
	local tool=strip m i
	[ -n "$WIN" ] && tool=x86_64-w64-mingw32-strip
	mkdir -p "$R/mods-$sys"
	for i in "${!mods[@]}"; do
		m=$R/mods-$sys/$(basename "${mods[$i]}")
		"$tool" -o "$m" "${mods[$i]}" || { echo "$tool failed on ${mods[$i]}"; exit 1; }
		mods[i]=$m
	done
}
strip_mods

# The path of Wine for a path of this machine, as winepath -w writes it: drive Z: is the root. Not winepath itself
# outside of a run: it would start the processes of the prefix on the display of the desktop, and the game, which
# runs in gamescope (headless.sh) and joins them, ends with BadWindow.
winpath() {
	printf 'Z:%s' "${1//\//\\}"
}

# the engine of build64 with another cl_update.cpp.o: $1 the version it takes itself for, $2 where it goes
make_exe() {
	local d=CMakeFiles/visionjk.x86_64.dir obj=$R/obj/cl_update_$sys-$1.o
	(
		cd "$b/codemp" || exit 1
		defs=$(grep '^CXX_DEFINES' $d/flags.make | cut -d= -f2-)
		incs=$(grep '^CXX_INCLUDES' $d/flags.make | cut -d= -f2-)
		flags=$(grep '^CXX_FLAGS' $d/flags.make | cut -d= -f2-)
		if [ -n "$WIN" ]; then
			eval "x86_64-w64-mingw32-g++ $defs $incs $flags '-DUPDATE_URL=\"$url\"' '-DUPDATE_VERSION=\"$1\"' -c '$src/codemp/client/cl_update.cpp' -o '$obj'" || exit 1
			# the build links an archive of its objects. Here it is the list of them, with the one object changed.
			sed "s|$d/client/cl_update.cpp.obj|$obj|" $d/objects1.rsp > "$R/obj/objects-$1.rsp"
			eval "$(tail -n 1 $d/link.txt | sed "s|-Wl,--whole-archive $d/objects.a -Wl,--no-whole-archive|@$R/obj/objects-$1.rsp|; s|-o ../visionjk.x86_64.exe|-o $2|; s|-Wl,--out-implib,[^ ]*||")"
		else
			eval "g++ $defs $incs $flags '-DUPDATE_URL=\"$url\"' '-DUPDATE_VERSION=\"$1\"' -c '$src/codemp/client/cl_update.cpp' -o '$obj'" || exit 1
			eval "$(sed "s|$d/client/cl_update.cpp.o|$obj|; s|-o ../visionjk.x86_64|-o $2|" $d/link.txt)"
		fi
	) || { echo "the test build of $1 failed"; exit 1; }
}

# A Wine prefix for this test and nothing else, made without a display so it opens no window. The folders of
# the user in it are links to the real ones of this machine as Wine makes them: they become folders of their own.
make_prefix() {
	local link
	[ -d "$WINEPREFIX/drive_c" ] && return 0
	echo "making the Wine prefix $WINEPREFIX"
	env -u DISPLAY -u WAYLAND_DISPLAY wineboot --init > "$R/wineboot.log" 2>&1
	wineserver -w
	[ -d "$WINEPREFIX/drive_c/users" ] || { echo "wineboot made no prefix, see $R/wineboot.log"; exit 1; }
	while IFS= read -r link; do
		rm "$link" && mkdir "$link"
	done < <(find "$WINEPREFIX/drive_c/users" -maxdepth 3 -type l)
}

# the folder of a player who has 0.0.1, or the program $1
make_game() {
	chmod -R u+w "$G" 2> /dev/null
	rm -rf "$G" "$H"
	mkdir -p "$G/base" "$H/base"
	cp "$X/${1:-v0.0.1}" "$G/$prog"
	cp "${libs[@]}" "$G/"
	cp "${mods[@]}" "$G/base/"
	# the original game, which no release brings and no update touches
	for p in "$BASE_ASSETS"/assets*.pk3; do
		ln -s "$p" "$G/base/"
	done
	# And the client assets, so the main menu is the one a player sees first: the game starts in japlus. A
	# file of its own and not a link, a release replaces it: the video of the main menu is read from it, and
	# up to 0.1.2 the game that started itself again on Windows held it and could not give it another name.
	mkdir -p "$G/japlus"
	cp "$gamedata/japlus/zz_japlus_client_assets.pk3" "$G/japlus/"
	echo "notes of 0.0.1" > "$G/notes.txt"
	echo "// my own settings" > "$G/base/extra.cfg"
}

# What the website has for 0.0.2: a new program, a file that changed, a .cfg the player has and one he does
# not have, a file in a folder that is new, and the libraries as they were.
make_release() {
	local stage=$R/stage/visionjk-0.0.2
	rm -rf "$R/stage" "$R/site"
	mkdir -p "$stage/base" "$stage/newdir" "$R/site/zips"
	cp "$X/v0.0.2" "$stage/$prog"
	cp "${libs[@]}" "$stage/"
	cp "${mods[@]}" "$stage/base/"
	if [ -n "$WIN" ]; then
		# A library the running program has loaded from its start, and which is another file in 0.0.2: some
		# bytes behind its end, which leaves it the library it was.
		echo "0.0.2" >> "$stage/SDL2.dll"
	fi
	# the asset pack as another file: some bytes behind its end, which leaves it the pack it was
	mkdir -p "$stage/japlus"
	cp "$gamedata/japlus/zz_japlus_client_assets.pk3" "$stage/japlus/"
	echo "0.0.2" >> "$stage/japlus/zz_japlus_client_assets.pk3"
	echo "notes of 0.0.2" > "$stage/notes.txt"
	echo "// the settings a release brings" > "$stage/base/extra.cfg"
	echo "// a config that is new in 0.0.2" > "$stage/base/new.cfg"
	echo "hello" > "$stage/newdir/hello.txt"
	(cd "$R/stage" && bsdtar -a -cf "$R/site/zips/visionjk-0.0.2-$sys-x86_64.zip" visionjk-0.0.2) || exit 1
	site_update 0.0.2
	tree=$R/site/update/0.0.2/$sys-x86_64
}

# fill_update of tools/build-game.sh as it is, for version $1: the zips in $R/site/zips (a development build:
# zips/dev), the lists and trees in $R/site/update, where the ones of earlier calls stay as on the website
site_update() {
	(
		set -euo pipefail
		version=$1
		DOWNLOADS=$R/site/zips
		notes=()
		summary=()
		# what fill_update calls of build-game.sh
		# shellcheck disable=SC2329
		die() {
			echo "$*" >&2
			exit 1
		}
		# shellcheck disable=SC2329
		zip_name() { echo "visionjk-$version-$1-x86_64.zip"; }
		eval "$(sed -n '/^\(channel_of\|version_key\|zip_dir\|fill_update\)() {/,/^}/p' "$WEBSITE/tools/build-game.sh")"
		if [ -d "$R/site/update" ]; then
			mv "$R/site/update" "$DOWNLOADS/update"
		fi
		fill_update
		mv "$DOWNLOADS/update" "$R/site/update"
	) || { echo "fill_update failed"; exit 1; }
}

