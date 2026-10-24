# shellcheck shell=bash disable=SC2034,SC2154 # results, failed, version and DOWNLOADS belong to the script that sources this
# What the test scripts share (smoke-test.sh, compat-test.sh): a game started without a window and without sound,
# its log, and the check of a run. Sourced after test-env.sh. A script that uses check_run keeps results=() and
# failed=0 itself.

# lines that fail a run wherever they are in its log (a line starts with the time)
FAIL_PATTERNS='ERROR:|Sys_Error|Segmentation fault|Signal caught|Received signal|failed to load|assertion'

die() {
	echo "$*" >&2
	exit 1
}

# cvars every run of VisionJK gets: its own home, no updater, no masterserver of the project, no sound. A stock
# OpenJK takes the ones it does not know as cvars of nobody.
common_args() {
	local home=$1
	printf '%s\n' +set fs_homepath "$home" +set v_updateCheck 0 \
		+set v_voiceMasterHost "" +set cl_motd 0 +set s_volume 0 +set s_musicvolume 0
}

# links the base assets of BASE_ASSETS into the base folder of a game
link_assets() {
	local pk3
	mkdir -p "$1/base"
	for pk3 in "$BASE_ASSETS"/assets*.pk3; do
		ln -sf "$pk3" "$1/base/$(basename "$pk3")"
	done
}

# Unpacks the zip of a release of a system into <into>/<system> (or copies the folder of from_dir) and prints the
# folder that holds the executable, with the base assets linked in. Takes DOWNLOADS, version and from_dir of the
# script.
unpack() {
	local into=$1 system=$2 exe=$3 zip found
	mkdir -p "$into/$system"
	if [ -n "${from_dir:-}" ]; then
		cp -a "$from_dir/." "$into/$system/"
	else
		zip=$DOWNLOADS/visionjk-$version-$system-x86_64.zip
		[ -f "$zip" ] || die "$zip is missing: build it with tools/build-game.sh $version"
		bsdtar -xf "$zip" -C "$into/$system"
	fi
	found=$(find "$into/$system" -type f -name "$exe")
	[ -n "$found" ] && [ "$(printf '%s\n' "$found" | wc -l)" = 1 ] || die "${zip:-$from_dir} does not have exactly one $exe"
	found=$(dirname "$found")
	link_assets "$found"
	echo "$found"
}

# The log of a run is what the game wrote to its output, the lines of its console with the time in front. The
# colour codes are taken out (Windows prints none) and so are the CRs of Windows.
plain_log() {
	sed 's/\^[0-9a-zA-Z]//g; s/\r$//' "$1" > "$2" 2> /dev/null || true
}

# Where the sanitizers of a build with them write their reports: one file per process, <log>.san.<pid>. Leaks
# are not looked for, the graphics drivers leave too many. tools/test/sanitize-suppressions.txt holds the undefined
# behaviour that is known. A build without the sanitizers ignores this.
sanitizer_env() {
	export ASAN_OPTIONS="log_path=$1.san:detect_leaks=0:abort_on_error=0"
	export UBSAN_OPTIONS="log_path=$1.san:print_stacktrace=1:suppressions=$TEST_DIR/sanitize-suppressions.txt"
}

# checks the log of a run: exit code 0, no line of FAIL_PATTERNS, each of the wanted lines, and no report of a
# sanitizer
check_run() {
	local name=$1 code=$2 log=$3
	shift 3
	local why=() want_line report
	[ "$code" = 0 ] || why+=("exit code $code")
	for report in "$log".san.*; do
		[ -e "$report" ] || continue
		why+=("$(basename "$report"): $(grep -m 1 -E 'ERROR|runtime error' "$report")")
	done
	if [ ! -s "$log" ]; then
		why+=("no log")
	else
		if grep -Eiq "$FAIL_PATTERNS" "$log"; then
			why+=("$(grep -Ei -m 3 "$FAIL_PATTERNS" "$log" | tr "\n" " ")")
		fi
		for want_line in "$@"; do
			grep -Fq -- "$want_line" "$log" || why+=("no line \"$want_line\"")
		done
	fi
	if [ ${#why[@]} = 0 ]; then
		results+=("PASS  $name")
	else
		results+=("FAIL  $name: ${why[*]}")
		failed=1
	fi
}

# Runs a client inside gamescope without an output (--backend headless): it gets a display of its own and no
# window ever reaches the desktop. DISPLAY and WAYLAND_DISPLAY of the desktop are taken away from it, so a client
# that does not go through gamescope fails instead of opening a window.
# gamescope 3.16 crashes on its way out after the game has ended, so the exit code is the one of the game, which
# a shell inside gamescope writes into a file.
run_client() {
	local log=$1 timeout_s=$2 dir=$3 code
	shift 3
	rm -f "$log.code"
	set +e
	# shellcheck disable=SC2016 # expanded by the shell inside gamescope
	(cd "$dir" && env -u DISPLAY -u WAYLAND_DISPLAY SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 \
		timeout -k 10 "$timeout_s" gamescope --backend headless -W 1024 -H 768 -w 1024 -h 768 -- \
		sh -c '"$@"; echo $? > "$0"' "$log.code" "$@") > "$log.out" 2>&1
	code=$?
	set -e
	if [ -s "$log.code" ]; then
		code=$(cat "$log.code")
	elif [ "$code" = 124 ]; then
		code="timed out after $timeout_s s"
	else
		code="the game did not end, gamescope $code"
	fi
	plain_log "$log.out" "$log"
	echo "$code"
}

# Starts a dedicated server in the background and returns once it runs its level (InitGame in its output), or
# fails. SERVER_PID is its process, stop_server ends it. The arguments are the command and its arguments; the
# server writes into <log>.out.
start_server() {
	local log=$1 dir=$2 timeout_s=$3
	shift 3
	: > "$log.out"
	(cd "$dir" && exec timeout -k 10 "$timeout_s" "$@") > "$log.out" 2>&1 &
	SERVER_PID=$!
	timeout 60 grep -q -m 1 'InitGame:' <(tail -n +1 -f "$log.out" --pid="$SERVER_PID") \
		|| die "the server of $log did not start, see $log.out"
}

# ends the server of start_server and makes its log
stop_server() {
	local log=$1
	kill "$SERVER_PID" 2> /dev/null || true
	wait "$SERVER_PID" 2> /dev/null || true
	plain_log "$log.out" "$log"
}
