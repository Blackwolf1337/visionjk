#!/bin/bash
# Starts the release zips of the site once before they are published: the client of each system loads a level and
# quits, the dedicated server of Linux loads a level, takes a bot and quits. Each run is checked by its exit code
# and its log. The zips are unpacked into build-test/smoke, the base assets of BASE_ASSETS are linked in, nothing of
# the site or of a game install is changed.
#
# usage: tools/test/smoke-test.sh <version> [options]
#   <version>                 the version whose zips are in VJK_DOWNLOADS (test-env.sh), for example 0.2.6
#   --only linux|windows      test one system
#   --keep                    leave build-test/smoke as it is after the run (it is emptied at the next run anyway)
#   --dir <folder>            test the Linux game in that folder (the one with visionjk.x86_64, for example a
#                             build with the sanitizers) instead of the zip. <version> is then what it says it is
#
# The clients run in gamescope without an output (no window on the desktop) and with SDL_AUDIODRIVER=dummy,
# Windows under Wine in a prefix of its own (build-test/smoke-wine). No run contacts a server of the project or a
# masterserver.
# Logs: build-test/smoke/<run>.log (linux-client.log and so on). A report of AddressSanitizer or
# UndefinedBehaviorSanitizer (build-test/smoke/<run>.san.*) fails a run. Exit code 0 when every run passed.
set -euo pipefail

# shellcheck source=tools/test/test-env.sh
. "$(dirname "${BASH_SOURCE[0]}")/test-env.sh"
# shellcheck source=tools/test/game-run.sh
. "$(dirname "${BASH_SOURCE[0]}")/game-run.sh"

# VJK_SMOKE_DIR and VJK_SMOKE_PORT: another folder and port of the server, for a second run at the same time
SMOKE=${VJK_SMOKE_DIR:-$WORK/smoke}
SERVER_PORT=${VJK_SMOKE_PORT:-29099}
WINEPREFIX_SMOKE=$WORK/smoke-wine
MAP=mp/ffa1
# seconds a run may take before it counts as hung, the first start of a Wine prefix takes longer
TIMEOUT_GAME=180
TIMEOUT_WINE=420
# frames the client stays in the level before it quits
FRAMES=600
BOT=Desann

failed=0
results=()

usage() {
	sed -n '2,19s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
	exit "${1:-1}"
}

want() {
	[ -z "$only" ] || [ "$only" = "$1" ]
}

# one client run: loads the level, takes a screenshot and quits
client_run() {
	local system=$1 timeout_s=$2 dir=$3 name home log code
	shift 3
	name=$system-client
	home=$SMOKE/home-$name
	log=$SMOKE/$name.log
	mapfile -t args < <(common_args "${HOME_PREFIX:-}$home")
	mkdir -p "$home"
	sanitizer_env "$log"
	code=$(run_client "$log" "$timeout_s" "$dir" "$@" "${args[@]}" \
		+set r_mode 4 +set r_fullscreen 0 +devmap "$MAP" +wait "$FRAMES" +screenshot smoke +wait 10 +quit)
	check_run "$system client" "$code" "$log" "${CLIENT_LINES[@]}"
	[ -f "$home/japlus/screenshots/smoke.jpg" ] || { results+=("FAIL  $system client: no screenshot"); failed=1; }
}

test_linux() {
	local game home code
	game=$(unpack "$SMOKE" linux visionjk.x86_64)

	client_run linux "$TIMEOUT_GAME" "$game" "$game/visionjk.x86_64"

	home=$SMOKE/home-linux-server
	mkdir -p "$home"
	mapfile -t args < <(common_args "$home")
	sanitizer_env "$SMOKE/linux-server.log"
	set +e
	(cd "$game" && timeout -k 10 "$TIMEOUT_GAME" ./visionjkded.x86_64 "${args[@]}" +set dedicated 1 \
		+set net_port "$SERVER_PORT" +set bot_enable 1 +map "$MAP" +wait 100 +addbot "$BOT" +wait 400 +quit) \
		> "$SMOKE/linux-server.log.out" 2>&1
	code=$?
	set -e
	plain_log "$SMOKE/linux-server.log.out" "$SMOKE/linux-server.log"
	check_run "linux dedicated server" "$code" "$SMOKE/linux-server.log" "${SERVER_LINES[@]}"
}

# ends every process of the prefix of the smoke test, and only of that one
stop_wine() {
	WINEPREFIX=$WINEPREFIX_SMOKE wineserver -k 2> /dev/null || true
	WINEPREFIX=$WINEPREFIX_SMOKE timeout 30 wineserver -w 2> /dev/null || true
}

test_windows() {
	local game
	command -v wine > /dev/null || die "wine is missing"
	game=$(unpack "$SMOKE" windows visionjk.x86_64.exe)
	# A new prefix is made without a display and without Mono and Gecko, so no dialog of Wine shows up. A run
	# ends the processes of the prefix after the game (wineserver -k): gamescope waits for every process it
	# started, and the services of Wine would keep it waiting. stop_wine ends them when a run did not get there.
	mkdir -p "$WINEPREFIX_SMOKE"
	export WINEPREFIX=$WINEPREFIX_SMOKE WINEDEBUG=-all
	trap stop_wine EXIT
	if [ ! -f "$WINEPREFIX_SMOKE/system.reg" ]; then
		env -u DISPLAY -u WAYLAND_DISPLAY WINEDLLOVERRIDES="mscoree,mshtml=" timeout "$TIMEOUT_WINE" wineboot -i \
			> "$SMOKE/wineboot.log" 2>&1 || die "wineboot failed, see $SMOKE/wineboot.log"
		stop_wine
	fi
	# shellcheck disable=SC2016 # expanded by the shell inside gamescope
	HOME_PREFIX=Z: client_run windows "$TIMEOUT_WINE" "$game" \
		sh -c 'wine "$@"; code=$?; wineserver -k; wineserver -w; exit $code' sh "$game/visionjk.x86_64.exe"
}

version=
only=
keep=0
from_dir=
while [ $# -gt 0 ]; do
	case $1 in
		--only) only=${2:-}; shift 2 ;;
		--keep) keep=1; shift ;;
		--dir) from_dir=${2:-}; shift 2 ;;
		-h|--help) usage 0 ;;
		-*) usage ;;
		*) [ -z "$version" ] || usage; version=$1; shift ;;
	esac
done
[ -n "$version" ] || usage
case $only in ''|linux|windows) ;; *) usage ;; esac
if [ -n "$from_dir" ]; then
	[ -f "$from_dir/visionjk.x86_64" ] || die "$from_dir has no visionjk.x86_64"
	from_dir=$(cd "$from_dir" && pwd)
	only=linux
fi
for tool in bsdtar gamescope timeout; do
	command -v "$tool" > /dev/null || die "$tool is missing"
done
[ -f "$BASE_ASSETS/assets0.pk3" ] || die "The base assets are not in $BASE_ASSETS"

# lines a good run has in its log
CLIENT_LINES=("Your client is VisionJK v$version " "CL_InitCGame:" "Server: $MAP")
SERVER_LINES=("gamename: VisionJK-MP: v$version" "InitGame:" "ClientConnect: 0 [Bot]" "ClientBegin: 0" "ShutdownGame")

rm -rf "$SMOKE"
mkdir -p "$SMOKE"
want linux && test_linux
want windows && test_windows

printf '%s\n' "${results[@]}"
# the unpacked games, the logs stay
[ "$keep" = 1 ] || rm -rf "$SMOKE/linux" "$SMOKE/windows"
exit "$failed"
