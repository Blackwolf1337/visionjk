#!/bin/bash
# Does a release get along with the games of others? A client of stock OpenJK (what most other clients are made
# from) joins the dedicated server of the release, and the client of the release joins a dedicated server of
# stock OpenJK. A run passes when the server lets the client in (ClientBegin), neither log has an error in it and
# the client ends by itself. Run it when the protocol, the connect or the JA+ parts changed.
#
# usage: tools/test/compat-test.sh <version> [options]
#   <version>                 the version whose Linux zip is in VJK_DOWNLOADS (test-env.sh), for example 0.2.6
#   --dir <folder>            test the Linux game in that folder instead of the zip (as tools/test/smoke-test.sh)
#   --keep                    leave build-test/compat as it is after the run
#
# Stock OpenJK is built once from the source in VJK_STOCK_SRC (test-env.sh) into build-test/openjk-stock (Release, the MP
# client, dedicated server, modules and rd-vanilla). The clients run in gamescope without an output and without
# sound, the servers on 127.0.0.1. Logs: build-test/compat/<run>-server.log and <run>-client.log. Exit code 0 when
# every run passed.
set -euo pipefail

# shellcheck source=tools/test/test-env.sh
. "$(dirname "${BASH_SOURCE[0]}")/test-env.sh"
# shellcheck source=tools/test/game-run.sh
. "$(dirname "${BASH_SOURCE[0]}")/game-run.sh"

COMPAT=$WORK/compat
STOCK_BUILD=$WORK/openjk-stock
MAP=mp/ffa1
PORT=29097
TIMEOUT_GAME=180
# frames the client stays after it typed connect: the connect, the level and some time in it
FRAMES=1500

failed=0
results=()

usage() {
	sed -n '2,15s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
	exit "${1:-1}"
}

# stock OpenJK, built once; prints the folder of the game made of it
stock_game() {
	local game=$COMPAT/stock module
	if [ ! -x "$STOCK_BUILD/openjkded.x86_64" ] || [ ! -x "$STOCK_BUILD/openjk.x86_64" ]; then
		[ -f "$STOCK_SRC/CMakeLists.txt" ] || die "The source of stock OpenJK is not in $STOCK_SRC"
		echo "building stock OpenJK" >&2
		cmake -S "$STOCK_SRC" -B "$STOCK_BUILD" -DCMAKE_BUILD_TYPE=Release -DBuildMPDed=ON -DBuildMPEngine=ON \
			-DBuildMPGame=ON -DBuildMPCGame=ON -DBuildMPUI=ON -DBuildMPRdVanilla=ON -DBuildSPEngine=OFF \
			-DBuildSPGame=OFF -DBuildSPRdVanilla=OFF -DBuildJK2SPEngine=OFF -DBuildJK2SPGame=OFF \
			-DBuildJK2SPRdVanilla=OFF -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
			> "$WORK/logs/compat-openjk-configure.log" 2>&1 || die "configure of stock OpenJK failed"
		nice -n "$BUILD_NICE" cmake --build "$STOCK_BUILD" -j "$BUILD_JOBS" > "$WORK/logs/compat-openjk-build.log" 2>&1 \
			|| die "build of stock OpenJK failed, see $WORK/logs/compat-openjk-build.log"
	fi
	mkdir -p "$game/base"
	cp "$STOCK_BUILD/openjk.x86_64" "$STOCK_BUILD/openjkded.x86_64" \
		"$STOCK_BUILD/codemp/rd-vanilla/rd-vanilla_x86_64.so" "$game/"
	for module in game/jampgamex86_64.so cgame/cgamex86_64.so ui/uix86_64.so; do
		cp "$STOCK_BUILD/codemp/$module" "$game/base/"
	done
	link_assets "$game"
	echo "$game"
}

# One run: a server of one game, a client of the other joins it and stays FRAMES frames. server and client are
# "<folder> <executable>".
join_run() {
	local name=$1 server_dir=$2 server_exe=$3 client_dir=$4 client_exe=$5 player=$6 code
	local slog=$COMPAT/$name-server.log clog=$COMPAT/$name-client.log
	shift 6
	mkdir -p "$COMPAT/home-$name-server" "$COMPAT/home-$name-client"

	mapfile -t args < <(common_args "$COMPAT/home-$name-server")
	sanitizer_env "$slog"
	start_server "$slog" "$server_dir" "$TIMEOUT_GAME" "./$server_exe" "${args[@]}" +set dedicated 1 \
		+set net_port "$PORT" +set sv_pure 0 +set g_needpass 0 +map "$MAP"

	mapfile -t args < <(common_args "$COMPAT/home-$name-client")
	sanitizer_env "$clog"
	code=$(run_client "$clog" "$TIMEOUT_GAME" "$client_dir" "./$client_exe" "${args[@]}" +set name "$player" \
		+set r_mode 4 +set r_fullscreen 0 +connect "127.0.0.1:$PORT" +wait "$FRAMES" +quit)
	stop_server "$slog"

	check_run "$name: server" 0 "$slog" "\"$player\"" "ClientBegin: 0"
	check_run "$name: client" "$code" "$clog" "CL_InitCGame:"
}

from_dir=
keep=0
version=
while [ $# -gt 0 ]; do
	case $1 in
		--dir) from_dir=${2:-}; shift 2 ;;
		--keep) keep=1; shift ;;
		-h|--help) usage 0 ;;
		-*) usage ;;
		*) [ -z "$version" ] || usage; version=$1; shift ;;
	esac
done
[ -n "$version" ] || usage
if [ -n "$from_dir" ]; then
	[ -f "$from_dir/visionjk.x86_64" ] || die "$from_dir has no visionjk.x86_64"
	from_dir=$(cd "$from_dir" && pwd)
fi
for tool in bsdtar gamescope timeout cmake; do
	command -v "$tool" > /dev/null || die "$tool is missing"
done
[ -f "$BASE_ASSETS/assets0.pk3" ] || die "The base assets are not in $BASE_ASSETS"

rm -rf "$COMPAT"
mkdir -p "$COMPAT" "$WORK/logs"
stock=$(stock_game)
vision=$(unpack "$COMPAT" linux visionjk.x86_64)

join_run stock-client "$vision" visionjkded.x86_64 "$stock" openjk.x86_64 Legacy
join_run vision-client "$stock" openjkded.x86_64 "$vision" visionjk.x86_64 Vision

printf '%s\n' "${results[@]}"
[ "$keep" = 1 ] || rm -rf "$COMPAT/stock" "$COMPAT/linux"
exit "$failed"
