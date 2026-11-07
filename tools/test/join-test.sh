#!/bin/bash
# How long joining a server and changing its level take (docs/japlus/loading.md, "Joining a server"). A dedicated
# server of the game in <folder> runs mp/ffa1 on 127.0.0.1 with bots, one client of the same folder joins it
# three times (disconnect and connect again in between), then the server changes the level once. The client runs
# with v_joinReport 1 and v_loadReport 1 and the script prints their lines: a join, its load, and the load of
# the level change.
#
# usage: tools/test/join-test.sh <folder> [options]
#   <folder>            a game folder with visionjk.x86_64, visionjkded.x86_64 and the modules (cmake --install)
#   --bots <n>          bots on the server, 0 to 31, default 31
#   --strace            run the client under strace too: the opens of files that failed, how many and how long
#                       they took, all of them and those of sounds (build-test/join/strace.txt)
#   --reuse             keep the home folder of the client of the run before (needs --keep there): its caches are
#                       filled, so the first join is that of a game started again
#   --pure              the server runs with sv_pure 1: after the change of the level the client has to send the
#                       checksums of its paks with the new feed, or the server drops it
#   --keep              leave build-test/join as it is after the run
#
# The client runs in gamescope without an output and without sound (headless.sh). Logs:
# build-test/join/server.log and client.log. The home folders are made new for every run (see --reuse), so the caches
# of the client (textures, sounds) are filled by the first join and used by the later ones.
set -euo pipefail
# shellcheck source=tools/test/test-env.sh
. "$(dirname "${BASH_SOURCE[0]}")/test-env.sh"
# shellcheck source=tools/test/game-run.sh
. "$(dirname "${BASH_SOURCE[0]}")/game-run.sh"
JOIN=$WORK/join
MAP=mp/ffa1
PORT=29098
TIMEOUT_GAME=300
JOINS=3
# frames the client stays in the level after a join, and in the menu after a disconnect
FRAMES_IN=1500
FRAMES_OUT=300

usage() {
	sed -n '2,16s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
	exit "${1:-1}"
}

bots=31
trace=0
keep=0
reuse=0
pure=0
dir=
while [ $# -gt 0 ]; do
	case $1 in
		--bots) bots=${2:-}; shift 2 ;;
		--strace) trace=1; shift ;;
		--keep) keep=1; shift ;;
		--reuse) reuse=1; shift ;;
		--pure) pure=1; shift ;;
		-h|--help) usage 0 ;;
		-*) usage ;;
		*) [ -z "$dir" ] || usage; dir=$1; shift ;;
	esac
done
[ -n "$dir" ] || usage
[ -x "$dir/visionjk.x86_64" ] && [ -x "$dir/visionjkded.x86_64" ] || die "$dir has no visionjk.x86_64 and visionjkded.x86_64"
dir=$(cd "$dir" && pwd)
[[ $bots =~ ^[0-9]+$ ]] && [ "$bots" -le 31 ] || usage
for tool in gamescope timeout; do
	command -v "$tool" > /dev/null || die "$tool is missing"
done
[ "$trace" = 0 ] || command -v strace > /dev/null || die "strace is missing"
[ -f "$BASE_ASSETS/assets0.pk3" ] || die "The base assets are not in $BASE_ASSETS"

if [ "$reuse" = 1 ]; then
	[ -d "$JOIN/home-client" ] || die "there is no home folder of a run before in $JOIN (--keep)"
	find "$JOIN" -mindepth 1 -maxdepth 1 ! -name home-client -exec rm -rf {} +
else
	rm -rf "$JOIN"
fi
mkdir -p "$JOIN/home-server" "$JOIN/home-client"
link_assets "$dir"
slog=$JOIN/server.log
clog=$JOIN/client.log

# The server reads its console from a fifo, so the script can change the level when the client is through with
# its joins.
mkfifo "$JOIN/server.in"
exec 3<> "$JOIN/server.in"
mapfile -t args < <(common_args "$JOIN/home-server")
: > "$slog.out"
(cd "$dir" && exec timeout -k 10 "$TIMEOUT_GAME" ./visionjkded.x86_64 "${args[@]}" +set dedicated 1 \
	+set net_port "$PORT" +set sv_pure "$pure" +set g_needpass 0 +set sv_lanForceRate 0 +set g_gametype 0 +set sv_maxclients "$((bots + 1))" \
	+map "$MAP") < "$JOIN/server.in" > "$slog.out" 2>&1 &
SERVER_PID=$!
trap 'stop_server "$slog"; exec 3>&-' EXIT
timeout 60 grep -q -m 1 'InitGame:' <(tail -n +1 -f "$slog.out" --pid="$SERVER_PID") \
	|| die "the server did not start, see $slog.out"
# bot_minplayers would add one every ten seconds: addbot, the names of the bots of the game in turn
names=(Alora Cultist Desann Reborn Shadowtrooper Snowtrooper Stormtrooper Swamptrooper Tavion Chiss Gran Mercenary Noghri Reelo Rodian Trandoshan Weequay Chewbacca Jedi Prisoner Rebel)
for i in $(seq 0 $((bots - 1))); do
	echo "addbot ${names[$((i % ${#names[@]}))]}" >&3
done
# the bots come in within a moment; one of them may be dropped again (a name twice): on when the count stands still
came=0
for _ in $(seq 1 $(( bots > 0 ? 60 : 0 ))); do
	sleep 2
	now=$(grep -c "ClientBegin:" "$slog.out" || true)
	if [ "$now" -gt 0 ] && [ "$now" = "$came" ]; then
		break
	fi
	came=$now
done
echo "bots on the server: $(grep -c 'ClientConnect: .*(BOT)' "$slog.out" || true) came, $(grep -c 'ClientDisconnect: .*(BOT)' "$slog.out" || true) left"

# The engine takes 32 command line words at most: what the client does is a config of its home folder.
{
	echo "set v_joinReport 1"
	echo "set v_loadReport 1"
	for i in $(seq 1 "$JOINS"); do
		echo "connect 127.0.0.1:$PORT"
		echo "wait $FRAMES_IN"
		if [ "$i" -lt "$JOINS" ]; then
			echo "disconnect"
			echo "wait $FRAMES_OUT"
		fi
	done
	echo "echo JOINTEST-JOINS-DONE"
	echo "wait $FRAMES_IN"
	echo "wait $FRAMES_IN"
	echo "echo JOINTEST-END"
	echo "quit"
} > "$JOIN/jointest.cfg"
for game in base japlus; do
	mkdir -p "$JOIN/home-client/$game"
	cp "$JOIN/jointest.cfg" "$JOIN/home-client/$game/"
done

mapfile -t args < <(common_args "$JOIN/home-client")
client=(./visionjk.x86_64)
if [ "$trace" = 1 ]; then
	calls=openat,stat,statx,newfstatat,access
	client=(strace -f -T -e "trace=$calls" -e status=failed -o "$JOIN/strace.txt" ./visionjk.x86_64)
fi
: > "$clog.out"
(cd "$dir" && HEADLESS_TIMEOUT=$TIMEOUT_GAME HEADLESS_SIZE="1024 768" HEADLESS_LOG=$clog.out \
	"$TEST_DIR/headless.sh" "${client[@]}" "${args[@]}" +set name "VisionJK join test" +set r_mode 4 \
	+set r_fullscreen 0 +exec jointest.cfg) &
client_pid=$!
if timeout "$TIMEOUT_GAME" grep -q -m 1 'JOINTEST-JOINS-DONE' <(tail -n +1 -f "$clog.out" --pid="$client_pid"); then
	echo "map $MAP" >&3
fi
wait "$client_pid" || echo "the client did not end by itself, see $clog.out" >&2
plain_log "$clog.out" "$clog"

grep -E '^[0-9-]+ [0-9:]+ (join of|load of|  +[0-9.]+ ms  )' "$clog" | sed -E 's/^[0-9-]+ [0-9:]+ //'
if [ "$pure" = 1 ]; then
	echo "pure server: $(cat "$slog.out" "$clog" | grep -c -i 'unpure' || true) lines of an unpure client in both logs"
	cat "$slog.out" "$clog" | grep -i 'unpure' | head -3 || true
fi
if [ "$trace" = 1 ]; then
	# the time each call took is at the end of its line: <0.000012>
	sum() {
		awk -F'<' '{ t += substr( $NF, 1, length( $NF ) - 1 ); n++ } END { printf "%d calls, %.1f ms\n", n, t * 1000 }'
	}
	echo "failed opens of the whole run of the client: $(grep -E 'openat|stat|access' "$JOIN/strace.txt" | sum)"
	echo "  of sound/ .wav: $(grep -E '"[^"]*/sound/[^"]*\.wav"' "$JOIN/strace.txt" | sum)"
	echo "  of sound/ .mp3: $(grep -E '"[^"]*/sound/[^"]*\.mp3"' "$JOIN/strace.txt" | sum)"
fi
[ "$keep" = 1 ] || rm -rf "$JOIN/home-server" "$JOIN/home-client"
