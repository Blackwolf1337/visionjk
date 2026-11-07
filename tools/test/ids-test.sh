#!/bin/bash
# The intrusion detection of the client (client/cl_ids.cpp) against a server that misbehaves on purpose:
# visionjkded-idstest (BuildDevIdsTest, server/sv_idstest.cpp) with vs_idsTest. For each scenario the test server
# runs on 127.0.0.1 and one headless client connects to it three times, so a server the client left is refused
# once and then joined again. The client log and its ids.log are checked.
#
# usage: tools/test/ids-test.sh [options] [scenario ...]
#   scenario            none cvars protected gamedir size unasked, default all of them
#   --build <folder>    the build folder with visionjkded-idstest (cmake -DBuildDevIdsTest=ON), default build64.
#                       It is installed into build-test/ids/install for the run
#   --keep              leave build-test/ids as it is after the run
#
# none: vs_idsTest 0, the client joins and ids.log stays empty. cvars: cvars the server may not set, written down,
# the client stays. protected: a protected cvar, the client leaves, the next connect is refused, the one after it
# leaves again. gamedir: fs_game with a path, the same. size: a download whose first block says half of its size,
# the same, and no .tmp of it may be left. unasked: download data nobody asked for (the client leaves at the third;
# it reads the whole message, so nothing of it is taken for the next commands). The size scenario runs the level
# mp/idstest, a copy of mp/duel4 in a pak only the server has. Logs: build-test/ids/<scenario>-server.log,
# <scenario>-client.log, <scenario>-ids.log, and <scenario>-shots/ with a screenshot of the first connect (for
# the notice of the IDS on the HUD, to look at).
set -euo pipefail
# shellcheck source=tools/test/test-env.sh
. "$(dirname "${BASH_SOURCE[0]}")/test-env.sh"
# shellcheck source=tools/test/game-run.sh
. "$(dirname "${BASH_SOURCE[0]}")/game-run.sh"
IDS=$WORK/ids
PORT=29097
TIMEOUT_GAME=240
FRAMES_IN=2400		# frames the client stays after a connect (about ten seconds: room for the download)
FRAMES_SHOT=500		# frames after the first connect for the screenshot, while a notice of the IDS is up
SCENARIOS=(none cvars protected gamedir size unasked)

usage() {
	sed -n '2,19s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
	exit "${1:-1}"
}

build=$GAME_REPO/build64
keep=0
wanted=()
while [ $# -gt 0 ]; do
	case $1 in
		--build) build=${2:-}; shift 2 ;;
		--keep) keep=1; shift ;;
		-h|--help) usage 0 ;;
		-*) usage ;;
		*) [[ " ${SCENARIOS[*]} " == *" $1 "* ]] || usage; wanted+=("$1"); shift ;;
	esac
done
[ ${#wanted[@]} -gt 0 ] || wanted=("${SCENARIOS[@]}")
exe=$(find "$build" -maxdepth 1 -type f -name 'visionjkded-idstest.*' | head -n 1)
[ -n "$exe" ] || die "$build has no visionjkded-idstest: configure it with -DBuildDevIdsTest=ON and build it"
for tool in gamescope timeout python3; do
	command -v "$tool" > /dev/null || die "$tool is missing"
done
[ -f "$BASE_ASSETS/assets0.pk3" ] || die "The base assets are not in $BASE_ASSETS"

rm -rf "$IDS"
mkdir -p "$IDS"
cmake --install "$build" --prefix "$IDS/install" > "$IDS/install.log" 2>&1 || die "cmake --install failed, see $IDS/install.log"
dir=$(dirname "$(find "$IDS/install" -type f -name 'visionjk.x86_64' | head -n 1)")
[ -x "$dir/visionjkded.x86_64" ] || die "the install in $IDS/install has no game"
cp "$exe" "$dir/"
link_assets "$dir"

# the pak only the server has: mp/idstest, a copy of mp/duel4 (the smallest level of the game, 1.1 MB)
make_pak() {
	python3 -I - "$BASE_ASSETS/assets0.pk3" "$1" <<-'EOF'
		import sys, zipfile
		with zipfile.ZipFile(sys.argv[1]) as src, zipfile.ZipFile(sys.argv[2], "w", zipfile.ZIP_DEFLATED) as out:
		    out.writestr("maps/mp/idstest.bsp", src.read("maps/mp/duel4.bsp"))
	EOF
}

results=()
failed=0

# check <name> <log> [+line | -line | =count:line] ...: +line must be in the log, -line must not, =n:line n times
check() {
	local name=$1 log=$2 rule n why=()
	shift 2
	for rule in "$@"; do
		case $rule in
			+*) grep -Fq -- "${rule:1}" "$log" || why+=("no line \"${rule:1}\"") ;;
			-*) ! grep -Fq -- "${rule:1}" "$log" || why+=("a line \"${rule:1}\"") ;;
			=*) n=${rule:1}; n=${n%%:*}; rule=${rule#*:}
				[ "$(grep -Fc -- "$rule" "$log" || true)" = "$n" ] || why+=("not $n lines \"$rule\"") ;;
		esac
	done
	if [ ${#why[@]} = 0 ]; then
		results+=("PASS  $name")
	else
		results+=("FAIL  $name: ${why[*]}")
		failed=1
	fi
}

run() {
	local name=$1 bits=$2 map=$3 slog=$IDS/$1-server.log clog=$IDS/$1-client.log home=$IDS/$1-home code
	mkdir -p "$home/server/base" "$home/client/base"
	[ "$map" = mp/idstest ] && make_pak "$home/server/base/zz_idstest.pk3"

	mapfile -t args < <(common_args "$home/server")
	start_server "$slog" "$dir" "$TIMEOUT_GAME" "./$(basename "$exe")" "${args[@]}" +set dedicated 1 \
		+set net_port "$PORT" +set sv_pure 0 +set g_needpass 0 +set g_gametype 0 +set sv_allowDownload 1 \
		+set vs_idsTest "$bits" +map "$map"

	# The engine takes 32 command line words at most: what the client does is a config of its home folder.
	{
		echo "connect 127.0.0.1:$PORT"
		echo "wait $FRAMES_SHOT"
		echo "screenshot"
		echo "wait $(( FRAMES_IN - FRAMES_SHOT ))"
		echo "connect 127.0.0.1:$PORT"
		echo "wait $FRAMES_IN"
		echo "connect 127.0.0.1:$PORT"
		echo "wait $FRAMES_IN"
		echo "v_idsstatus"
		echo "v_idsSeqShown"
		echo "quit"
	} > "$home/client/base/idstest.cfg"
	mapfile -t args < <(common_args "$home/client")
	code=$(run_client "$clog" "$TIMEOUT_GAME" "$dir" ./visionjk.x86_64 "${args[@]}" +set name "VisionJK IDS test" \
		+set r_mode 4 +set r_fullscreen 0 +set cl_allowDownload 1 +set cl_downloadPrompt 0 +set v_downloadReview 0 \
		+set v_downloadHttp 0 +exec idstest.cfg)
	stop_server "$slog"
	# (in the folder of the game the client runs, japlus by default)
	find "$home/client" -name ids.log -exec cat {} + > "$IDS/$name-ids.log"
	mkdir -p "$IDS/$name-shots"
	find "$home/client" -path '*screenshots*' -type f -exec cp {} "$IDS/$name-shots/" \;
	[ "$code" = 0 ] || { results+=("FAIL  $name: client exit code $code"); failed=1; return; }

	local from="the server 127.0.0.1:$PORT sent"
	case $name in
		none)
			check "$name" "$clog" "-IDS:" "-Left the server" "-You left" "+  nothing this run"
			check "$name: joined" "$slog" "=3:ClientBegin:" ;;
		cvars)
			check "$name" "$clog" "+IDS: $from cvars it may not set: cl_allowDownload=0, rconPassword=idstest" \
				"-Left the server" "-You left" "+  the server 127.0.0.1:$PORT sent cvars it may not set" \
				"-servers left this run:" '+Cvar v_idsSeqShown = "3"'
			check "$name: joined" "$slog" "=3:ClientBegin:" ;;
		protected)
			check "$name" "$clog" "+IDS: $from a cvar no server may set: fs_basepath=/idstest" \
				"=2:Left the server: it sent a cvar no server may set" \
				"=1:You left 127.0.0.1:$PORT before: it sent a cvar no server may set" \
				"+servers left this run:" "+  127.0.0.1:$PORT, it sent a cvar no server may set (the next connect" ;;
		gamedir)
			check "$name" "$clog" "+IDS: $from a path in the place of its game folder: ../idstest" \
				"=2:Left the server: it sent a path in the place of its game folder" \
				"=1:You left 127.0.0.1:$PORT before: it sent a path in the place of its game folder" ;;
		size)
			check "$name" "$clog" "+IDS: $from more data of a download than it said it has" \
				"=2:Left the server: it sent more data of a download" "=1:You left 127.0.0.1:$PORT before"
			# the client left in the middle of the download: nothing of it may be left
			find "$home/client" -name '*.tmp' > "$IDS/$name-tmp.txt"
			check "$name: no .tmp left" "$IDS/$name-tmp.txt" "-.tmp" ;;
		unasked)
			check "$name" "$clog" "+IDS: $from data of a download that was not asked for" \
				"=2:Left the server: it sent data of a download that was not asked for" \
				"=1:You left 127.0.0.1:$PORT before" "-a message that can not be read" "-Illegible server message" ;;
	esac
	check "$name: ids.log" "$IDS/$name-ids.log" "$([ "$name" = none ] && echo "-sent" || echo "+sent")"
}

for name in "${wanted[@]}"; do
	case $name in
		none) run none 0 mp/ffa1 ;;
		cvars) run cvars 1 mp/ffa1 ;;
		protected) run protected 2 mp/ffa1 ;;
		gamedir) run gamedir 4 mp/ffa1 ;;
		size) run size 8 mp/idstest ;;
		unasked) run unasked 16 mp/ffa1 ;;
	esac
done

printf '%s\n' "${results[@]}"
[ "$keep" = 1 ] || rm -rf "$IDS/install" "$IDS"/*-home
exit "$failed"
