#!/bin/bash
# End to end test of the updater of the client on Linux, with no sound and no window in sight: a game folder
# of a made up version 0.0.1 under build-test/updater, a web server on the loopback address that has
# version 0.0.2, and the client started in gamescope without an output (../headless.sh), where it finds the
# newer version, fetches it, puts it in place and starts again.
#
# The two programs are the engine of build64 with client/cl_update.cpp built once more for each: with the
# address of the test server and with a version of its own (what -DUpdateURL and -DUpdateVersion do). Every
# other object file is the one of build64. The file lists of the server are written by fill_update of
# tools/build-game.sh of the website repository (VJK_WEBSITE), as it is.
#
# WIN=1: the same with the Windows build (buildwin64) under Wine, in a Wine prefix of its own under
# build-test/updater/wine that is made here if it is not there. That shows that the Windows code runs: WinHTTP,
# the paths, the start of the game by itself. It does not show how Windows itself takes it, Wine is another
# program. The game is called jptest.exe there.
#
# usage: [WIN=1] tools/test/updater/e2e.sh [names of scenarios, default all]
# scenarios: update damaged cutshort undo off down address devbuild elsewhere repair shadow same
# shellcheck source=tools/test/updater/lib.sh
. "$(dirname "$0")/lib.sh"
failed=0
checked=0
server=

check() {	# check <what> <command...>: the command has to succeed
	local what=$1
	shift
	checked=$((checked + 1))
	if "$@" > /dev/null 2>&1; then
		echo "  ok      $what"
	else
		echo "  FAILED  $what"
		failed=$((failed + 1))
	fi
}

has() { grep -a -q -F -- "$2" "$R/$1.log"; }
hasnot() { ! grep -a -q -F -- "$2" "$R/$1.log"; }
asked() { grep -q -F -- "\"GET $1 " "$R/server.log"; }
notasked() { ! grep -q -F -- "\"GET $1 " "$R/server.log"; }
same() { cmp -s "$1" "$2"; }
text_is() { [ "$(cat "$1" 2> /dev/null)" = "$2" ]; }
no_leftovers() { [ -z "$(find "$G" \( -name '*.vjknew' -o -name '*.vjkold' -o -name 'visionjk-update.txt*' \) -print -quit)" ]; }
runnable() { [ -n "$WIN" ] || test -x "$1"; }
nofetch() { ! grep -q -F -- '"GET /update/0.0.2/' "$R/server.log"; }
aside() { [ -e "$1.vjkold" ] && [ ! -e "$1" ]; }
# another file of the same size: its last byte is another one
spoil() { printf 'X' | dd of="$1" bs=1 seek=$(($(stat -c %s "$1") - 1)) conv=notrunc status=none; }

# the folder of a player who has 0.0.2, every file as the website has it but the .cfg that came new with it
make_game_2() {
	make_game v0.0.2
	mkdir -p "$G/newdir"
	cp "$tree/notes.txt" "$G/notes.txt"
	cp "$tree/newdir/hello.txt" "$G/newdir/"
	cp "$tree/japlus/zz_japlus_client_assets.pk3" "$G/japlus/"
	if [ -n "$WIN" ]; then
		cp "$tree/SDL2.dll" "$G/"
	fi
}

# Windows does not let a file be deleted that a program still runs from, and the game that starts as the new
# version comes up while the one before it is on its way out. So there the old files are gone one start later.
settle() {
	[ -n "$WIN" ] || return 0
	echo "  (old files that were still there after it: $(find "$G" -name '*.vjkold' | wc -l). One more start.)"
	run settle 'wait 200\nquit\n'
}

server_start() {
	: > "$R/server.log"
	python3 -m http.server "$port" --bind 127.0.0.1 --directory "$R/site" >> "$R/server.log" 2>&1 &
	server=$!
	sleep 1		# a test script, not game code
}

server_stop() {
	[ -n "$server" ] && kill "$server" 2> /dev/null && wait "$server" 2> /dev/null
	server=
}

# run <name of the log> <the console script> [more arguments]: the game from its folder, until it ends
run() {
	local name=$1
	shift
	printf '%b' "$1" > "$H/base/updtest.cfg"
	shift
	: > "$R/$name.log"
	if [ -n "$WIN" ]; then
		# The program that asked for the update ends when it has started the next one: wineserver -w waits
		# for all of them.
		cat > "$R/start.sh" <<- EOF
			#!/bin/bash
			export WINEPREFIX="$WINEPREFIX" WINEDLLOVERRIDES="$WINEDLLOVERRIDES" WINEDEBUG=-all
			cd "$G" || exit 1
			wine $prog +set fs_homepath "\$(winepath -w "$H")" +set cl_renderer rd-vanilla +set r_fullscreen 0 \\
				+set r_mode -1 +set r_customwidth 1280 +set r_customheight 720 +set s_initsound 0 +set in_nograb 1 +set com_maxfps 60 \\
				+set r_swapInterval 0 "\$@" +exec updtest.cfg >> "$R/$name.log" 2>&1
			wineserver -w
		EOF
	else
		cat > "$R/start.sh" <<- EOF
			#!/bin/bash
			cd "$G" && exec ./$prog +set fs_homepath "$H" +set cl_renderer rd-vanilla +set r_fullscreen 0 \\
				+set r_mode -1 +set r_customwidth 1280 +set r_customheight 720 +set s_initsound 0 +set in_nograb 1 +set com_maxfps 60 \\
				+set r_swapInterval 0 "\$@" +exec updtest.cfg >> "$R/$name.log" 2>&1
		EOF
	fi
	chmod +x "$R/start.sh"
	HEADLESS_TIMEOUT=150 "$TEST_DIR/headless.sh" "$R/start.sh" "$@"
}

trap 'server_stop; chmod -R u+w "$G" 2> /dev/null' EXIT

mkdir -p "$R/obj" "$X"
if [ -n "$WIN" ]; then
	make_prefix
fi
echo "building the programs of the test ($sys)"
make_exe v0.0.1 "$X/v0.0.1"
make_exe v0.0.2 "$X/v0.0.2"
# (a name with a dot in it: the linker for Windows adds .exe to one without)
make_exe v0.0.1-5-gabc1234 "$X/dev.0"
make_release
want=${*:-update damaged cutshort undo off down address devbuild elsewhere repair shadow same}

for scenario in $want; do
	case "$scenario" in

	update)
		echo "== update: 0.0.1 finds 0.0.2, fetches it, puts it in place and starts again"
		make_game
		server_start
		run update 'wait 600\necho UPDTEST-ASKED\nv_updateState\nv_updateText\nscreenshot\nwait 60\nupdate\nwait 1500\necho UPDTEST-END\nv_updateState\nquit\n'
		server_stop
		check "the console says that 0.0.2 is out" has update "Version 0.0.2 of the game is out, this is 0.0.1"
		check "v_updateState is 1 and the line for the menu is there" has update "Version 0.0.2 is out. Click here to update"
		check "the files are fetched and the game starts again" has update "Version 0.0.2 is fetched. The game starts again"
		check "the next start says that it was updated" has update "The game was updated to version 0.0.2."
		check "and it is 0.0.2 now, with nothing newer" has update "This is version 0.0.2, and the newest one is 0.0.2."
		check "the new program ran its script to the end" has update "UPDTEST-END"
		check "the program is the one of 0.0.2" same "$G/$prog" "$X/v0.0.2"
		check "and can still be run" runnable "$G/$prog"
		settle
		check "the file that changed is the new one" text_is "$G/notes.txt" "notes of 0.0.2"
		check "the .cfg the player had is his own still" text_is "$G/base/extra.cfg" "// my own settings"
		check "the .cfg he did not have is there" text_is "$G/base/new.cfg" "// a config that is new in 0.0.2"
		check "the file in the new folder is there" text_is "$G/newdir/hello.txt" "hello"
		check "the original game is still a link to it" test -L "$G/base/assets0.pk3"
		if [ -n "$WIN" ]; then
			check "the library the program had loaded while it was replaced is the new one" same "$G/SDL2.dll" "$tree/SDL2.dll"
		fi
		check "the asset pack, which the main menu had its video from, is the new one" same "$G/japlus/zz_japlus_client_assets.pk3" "$tree/japlus/zz_japlus_client_assets.pk3"
		check "no .vjknew, .vjkold or journal is left" no_leftovers
		check "the list was asked for" asked /update/$sys-x86_64.txt
		check "the program was fetched" asked "/update/0.0.2/$sys-x86_64/$prog"
		check "the changed file was fetched" asked /update/0.0.2/$sys-x86_64/notes.txt
		check "a library that is the same was not fetched" notasked "/update/0.0.2/$sys-x86_64/base/$(basename "${mods[0]}")"
		check "the .cfg the player has was not fetched" notasked /update/0.0.2/$sys-x86_64/base/extra.cfg
		;;

	damaged)
		echo "== damaged: a file of the server is not the one of the list"
		make_game
		cp "$tree/notes.txt" "$R/notes.keep"
		echo "notes of 0.0.X" > "$tree/notes.txt"
		server_start
		run damaged 'wait 600\nupdate\nwait 600\necho UPDTEST-END\nv_updateState\nv_updateText\nscreenshot\nwait 60\nquit\n'
		check "the console says which file" has damaged "notes.txt came in damaged"
		check "and that nothing was changed" has damaged "The update failed, nothing was changed."
		check "the line of the menu offers another try" has damaged "The update failed, the console says why. Click here to try again"
		check "the game ran on" has damaged "UPDTEST-END"
		check "the program is still the one of 0.0.1" same "$G/$prog" "$X/v0.0.1"
		check "the file is still the old one" text_is "$G/notes.txt" "notes of 0.0.1"
		check "there is no journal" test ! -e "$G/visionjk-update.txt"
		check "the damaged file was not kept" test ! -e "$G/notes.txt.vjknew"
		check "what came in right before it is kept" same "$G/base/new.cfg.vjknew" "$tree/base/new.cfg"
		# the server is right again: the next try takes what is there and fetches the rest
		cp "$R/notes.keep" "$tree/notes.txt"
		: > "$R/server.log"
		run damaged2 'wait 600\nupdate\nwait 1500\necho UPDTEST-END\nquit\n'
		server_stop
		check "the second try goes through" has damaged2 "The game was updated to version 0.0.2."
		check "the program is the one of 0.0.2" same "$G/$prog" "$X/v0.0.2"
		check "the file that was kept was not fetched again" notasked /update/0.0.2/$sys-x86_64/base/new.cfg
		check "the one that was damaged was" asked /update/0.0.2/$sys-x86_64/notes.txt
		settle
		check "no .vjknew, .vjkold or journal is left" no_leftovers
		;;

	cutshort)
		echo "== cutshort: the machine went off in the middle of a swap"
		make_game
		mkdir -p "$G/newdir"
		for f in base/new.cfg newdir/hello.txt notes.txt "$prog"; do
			cp "$tree/$f" "$G/$f.vjknew"
		done
		# one of the four was moved already
		mv "$G/notes.txt" "$G/notes.txt.vjkold"
		mv "$G/notes.txt.vjknew" "$G/notes.txt"
		printf 'visionjk-update-journal 1\nswap 0.0.2\nnotes.txt\nbase/new.cfg\nnewdir/hello.txt\n%s\n' "$prog" > "$G/visionjk-update.txt"
		server_start
		run cutshort 'wait 500\necho UPDTEST-END\nquit\n'
		server_stop
		check "the start finishes it and says that the game was updated" has cutshort "The game was updated to version 0.0.2."
		check "the program is the one of 0.0.2" same "$G/$prog" "$X/v0.0.2"
		check "and can be run" runnable "$G/$prog"
		check "the other files are in place" text_is "$G/newdir/hello.txt" "hello"
		check "the one that was moved before stayed" text_is "$G/notes.txt" "notes of 0.0.2"
		settle
		check "no .vjknew, .vjkold or journal is left" no_leftovers
		check "the game ran" has cutshort "UPDTEST-END"
		;;

	undo)
		echo "== undo: one file can not be put in place"
		make_game
		mkdir -p "$G/newdir"
		for f in base/new.cfg newdir/hello.txt notes.txt "$prog"; do
			cp "$tree/$f" "$G/$f.vjknew"
		done
		printf 'visionjk-update-journal 1\nswap 0.0.2\nnotes.txt\nnewdir/hello.txt\nbase/new.cfg\n%s\n' "$prog" > "$G/visionjk-update.txt"
		# nothing in base can get another name
		chmod a-w "$G/base"
		server_start
		run undo 'wait 500\necho UPDTEST-END\nv_updateState\nquit\n'
		server_stop
		chmod u+w "$G/base"
		check "the console says which file it was" has undo "The update could not be put in place, nothing was changed: base/new.cfg can not be replaced."
		check "the game started as 0.0.1 and offers 0.0.2 again" has undo "Version 0.0.2 of the game is out, this is 0.0.1"
		check "the program is the one of 0.0.1" same "$G/$prog" "$X/v0.0.1"
		check "the file that had been moved is the old one again" text_is "$G/notes.txt" "notes of 0.0.1"
		check "and its new one waits again" text_is "$G/notes.txt.vjknew" "notes of 0.0.2"
		check "the file of the new folder is not in place" test ! -e "$G/newdir/hello.txt"
		check "no old file is left over" test ! -e "$G/notes.txt.vjkold"
		check "the journal is gone" test ! -e "$G/visionjk-update.txt"
		check "the game ran" has undo "UPDTEST-END"
		;;

	off)
		echo "== off: v_updateCheck 0"
		make_game
		server_start
		run off 'wait 600\necho UPDTEST-END\nv_updateState\nquit\n' +set v_updateCheck 0
		server_stop
		check "the server was not asked anything" test ! -s "$R/server.log"
		check "the console says nothing of a version" hasnot off "of the game is out"
		check "the game ran" has off "UPDTEST-END"
		;;

	down)
		echo "== down: the website is not there"
		make_game
		run down 'wait 400\necho UPDTEST-QUIET\nv_update_check\nwait 400\necho UPDTEST-END\nquit\n'
		check "the start says nothing about it" sh -c "! sed -n '1,/UPDTEST-QUIET/p' '$R/down.log' | grep -a -q 'did not say whether'"
		check "v_update_check says why" has down "did not say whether there is a newer version"
		check "the game ran" has down "UPDTEST-END"
		check "nothing was written into the folder of the game" no_leftovers
		;;

	address)
		echo "== address: who can say where the game asks"
		make_game
		server_start
		run address1 'wait 500\necho UPDTEST-END\nquit\n' +set v_updateURL "http://127.0.0.1:$port/other"
		check "an address without https on the command line is refused" has address1 "v_updateURL on the command line is no https address"
		check "and the address of the build is asked" asked /update/$sys-x86_64.txt
		check "the other one is not" notasked /other/$sys-x86_64.txt
		# a config file tries
		: > "$R/server.log"
		printf 'seta v_updateURL "http://127.0.0.1:%s/other"\nset v_updateURL "https://127.0.0.1:%s/other"\n' "$port" "$port" > "$H/base/autoexec.cfg"
		run address2 'wait 500\necho UPDTEST-END\nv_updateURL\nquit\n'
		check "a config file can not set it" has address2 "v_updateURL is read only"
		check "the address of the build is asked" asked /update/$sys-x86_64.txt
		check "the other one is not" notasked /other/$sys-x86_64.txt
		rm -f "$H/base/autoexec.cfg"
		# the console tries, then the command line with https: taken, and this server does not speak https
		: > "$R/server.log"
		run address3 'set v_updateURL https://127.0.0.1:1/x\nwait 300\nv_update_check\nwait 500\necho UPDTEST-END\nquit\n' +set v_updateURL "https://127.0.0.1:$port/update"
		server_stop
		check "an https address on the command line is taken" has address3 "Updates are asked for at https://127.0.0.1:$port/update, as the command line says."
		check "the console can not change it" has address3 "v_updateURL is read only"
		check "a server without a certificate gives no update" has address3 "did not say whether there is a newer version"
		check "and no file list was read from it" hasnot address3 "of the game is out"
		;;

	devbuild)
		echo "== devbuild: a build between two releases"
		make_game dev.0
		server_start
		run devbuild 'wait 400\nv_update_check\nupdate\nwait 300\necho UPDTEST-END\nquit\n'
		server_stop
		check "it says that there is no update for it" has devbuild "This is a build between two releases (v0.0.1-5-gabc1234), there is no update for those."
		check "the server was not asked anything" test ! -s "$R/server.log"
		check "the program is its own still" same "$G/$prog" "$X/dev.0"
		;;

	elsewhere)
		echo "== elsewhere: the game reads its files from another folder than its program's"
		make_game
		server_start
		# a second folder that has the game files, as links to the ones of the first
		rm -rf "$R/elsewhere"
		mkdir -p "$R/elsewhere"
		ln -s "$G/base" "$G/japlus" "$R/elsewhere/"
		if [ -n "$WIN" ]; then
			run elsewhere 'wait 400\nv_update_check\nwait 300\necho UPDTEST-END\nquit\n' +set fs_basepath "$(winpath "$R/elsewhere")"
		else
			run elsewhere 'wait 400\nv_update_check\nwait 300\necho UPDTEST-END\nquit\n' +set fs_basepath "$R/elsewhere"
		fi
		cp "$R/server.log" "$R/server-elsewhere.log"
		# and the folder of the program itself, said another way: that is not another folder
		: > "$R/server.log"
		if [ -n "$WIN" ]; then
			run samefolder 'wait 500\necho UPDTEST-END\nquit\n' +set fs_basepath "Z:$G/."
		else
			run samefolder 'wait 500\necho UPDTEST-END\nquit\n' +set fs_basepath "$G/./"
		fi
		server_stop
		check "it says that it does not update itself" has elsewhere "it does not update itself"
		check "the server was not asked anything by it" test ! -s "$R/server-elsewhere.log"
		check "the folder of the program under another spelling is asked for updates as ever" has samefolder "Version 0.0.2 of the game is out"
		;;

	repair)
		echo "== repair: the program is 0.0.2 and three of its files are not (another version, same size and other bytes, missing)"
		make_game_2
		gm=base/$(basename "${mods[2]}")
		echo "notes of 0.0.1" > "$G/notes.txt"
		# the game module, which the main menu has not loaded
		spoil "$G/$gm"
		rm "$G/newdir/hello.txt"
		server_start
		run repair 'wait 600\necho UPDTEST-ASKED\nv_updateState\nv_updateText\nscreenshot\nwait 60\nupdate\nwait 1500\necho UPDTEST-END\nv_updateState\nquit\n'
		server_stop
		check "the console says that files are not the ones of the version" has repair "This is version 0.0.2, but 3 of its files are missing or are not the ones of that version"
		check "the line for the menu offers the repair" has repair "Files of version 0.0.2 are missing or changed. Click here to repair"
		check "the files are fetched and the game starts again" has repair "Version 0.0.2 is fetched. The game starts again"
		check "the start after it finds nothing wrong" has repair "This is version 0.0.2, and the newest one is 0.0.2."
		check "the game ran its script to the end" has repair "UPDTEST-END"
		settle
		check "the file of another version is the one of 0.0.2" text_is "$G/notes.txt" "notes of 0.0.2"
		check "the module of the same size and other bytes is the one of 0.0.2" same "$G/$gm" "$tree/$gm"
		check "the file that was missing is there" text_is "$G/newdir/hello.txt" "hello"
		check "the .cfg the player does not have was not brought" test ! -e "$G/base/new.cfg"
		check "the program is the one of 0.0.2 still" same "$G/$prog" "$X/v0.0.2"
		check "and can be run" runnable "$G/$prog"
		check "no .vjknew, .vjkold or journal is left" no_leftovers
		check "the file of another version was fetched" asked /update/0.0.2/$sys-x86_64/notes.txt
		check "the module was fetched" asked "/update/0.0.2/$sys-x86_64/$gm"
		check "the missing file was fetched" asked /update/0.0.2/$sys-x86_64/newdir/hello.txt
		check "the program was not fetched" notasked "/update/0.0.2/$sys-x86_64/$prog"
		check "the asset pack, which is right, was not fetched" notasked /update/0.0.2/$sys-x86_64/japlus/zz_japlus_client_assets.pk3
		check "the .cfg he does not have was not fetched" notasked /update/0.0.2/$sys-x86_64/base/new.cfg
		;;

	shadow)
		echo "== shadow: modules of another version in the folder of the player, which the game loads before its own"
		make_game_2
		ui=base/$(basename "${mods[0]}")
		cg=base/$(basename "${mods[1]}")
		gm=base/$(basename "${mods[2]}")
		# The ui module with some bytes behind its end, which leaves it the module it was: the game loads this
		# one and has it loaded while it is given another name.
		cp "$G/$ui" "$H/$ui"
		echo "old" >> "$H/$ui"
		cp "$G/$cg" "$H/$cg"
		spoil "$H/$cg"
		# and one that is the module of the release, which is in nobody's way
		cp "$G/$gm" "$H/$gm"
		server_start
		run shadow 'wait 600\necho UPDTEST-ASKED\nv_updateState\nv_updateText\nscreenshot\nwait 60\nupdate\nwait 1500\necho UPDTEST-END\nv_updateState\nquit\n'
		check "the console names the folder of the player" has shadow "Game modules in"
		check "and says that there is more than one" has shadow " and more). update gives them another name."
		check "the line for the menu offers the repair" has shadow "Old modules are used in place of those of version 0.0.2. Click here to repair"
		check "the game starts again" has shadow "The game starts again and loads its own modules."
		check "the start after it finds nothing wrong" has shadow "This is version 0.0.2, and the newest one is 0.0.2."
		check "the game ran its script to the end" has shadow "UPDTEST-END"
		check "the ui module of the player, which was loaded, has another name" aside "$H/$ui"
		check "the cgame module too" aside "$H/$cg"
		check "the one that was the module of the release stayed" same "$H/$gm" "$tree/$gm"
		check "the folder of the game was not touched" no_leftovers
		check "the modules of the game are the ones of 0.0.2" same "$G/$ui" "$tree/$ui"
		check "no file was fetched" nofetch
		# a newer version is out and a module of the player is in the way: the update takes it out of the way too
		make_game
		cp "$G/$cg" "$H/$cg"
		spoil "$H/$cg"
		: > "$R/server.log"
		run shadow2 'wait 600\nupdate\nwait 1500\necho UPDTEST-END\nquit\n'
		server_stop
		check "with an update: the console names the module of the player" has shadow2 "Game modules in"
		check "with an update: it has another name afterwards" aside "$H/$cg"
		check "with an update: the game was updated" has shadow2 "The game was updated to version 0.0.2."
		check "with an update: nothing is left to do" has shadow2 "This is version 0.0.2, and the newest one is 0.0.2."
		settle
		check "with an update: no .vjknew, .vjkold or journal is left in the folder of the game" no_leftovers
		;;

	same)
		echo "== same: the newest version with every file right, and a module of the release in the folder of the player"
		make_game_2
		cp "$G/base/$(basename "${mods[1]}")" "$H/base/"
		server_start
		run same 'wait 600\necho UPDTEST-ASKED\nv_updateState\nv_updateText\nv_update_check\nwait 600\necho UPDTEST-END\nquit\n'
		server_stop
		check "the list was asked for" asked /update/$sys-x86_64.txt
		check "nothing is offered" hasnot same "Click here"
		check "no file is called wrong" hasnot same "are not the ones of"
		check "v_update_check says that this is the newest" has same "This is version 0.0.2, and the newest one is 0.0.2."
		check "no file was fetched" nofetch
		check "the module of the player was left alone" test -e "$H/base/$(basename "${mods[1]}")"
		check "the game ran its script to the end" has same "UPDTEST-END"
		check "nothing was written into the folder of the game" no_leftovers
		;;

	*) echo "no such scenario: $scenario" ;;
	esac
done

echo "$checked checks, $failed failed"
[ "$failed" = 0 ]
