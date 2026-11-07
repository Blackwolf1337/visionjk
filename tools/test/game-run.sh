# shellcheck shell=bash disable=SC2034,SC2154 # results, failed, version and DOWNLOADS belong to the script that sources this
# What the test scripts share (smoke-test.sh, compat-test.sh): a game started without a window and without sound,
# its log, and the check of a run. Sourced after test-env.sh. A script that uses check_run keeps results=() and
# failed=0 itself.

# lines that fail a run wherever they are in its log (a line starts with the time), but the quit of a dedicated server
# that stop_server ends with SIGTERM (sys_unix.cpp quits on it since 0.2.9)
FAIL_PATTERNS='ERROR:|Sys_Error|Segmentation fault|Signal caught|Received signal|failed to load|assertion'
FAIL_EXCEPT='Received signal 15, quitting'

die() {
	echo "$*" >&2
	exit 1
}

# cvars every run of VisionJK gets: its own home, no updater, no masterserver of the project, no sound. A stock
# OpenJK takes the ones it does not know as cvars of nobody.
common_args() {
	local home=$1
	printf '%s\n' +set fs_homepath "$home" +set v_updateCheck 0 \
		+set v_master "" +set cl_motd 0 +set s_volume 0 +set s_musicvolume 0
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
		# (the zips of a development build are in downloads/dev)
		[[ $version == *-dev.* ]] && zip=$DOWNLOADS/dev/visionjk-$version-$system-x86_64.zip
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
		if grep -Ei "$FAIL_PATTERNS" "$log" | grep -Fqv "$FAIL_EXCEPT"; then
			why+=("$(grep -Ei "$FAIL_PATTERNS" "$log" | grep -Fv "$FAIL_EXCEPT" | head -n 3 | tr "\n" " ")")
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

# Runs a client with no window on the desktop and no sound (headless.sh: gamescope without an output) and prints
# its exit code, or why there is none.
run_client() {
	local log=$1 timeout_s=$2 dir=$3 code
	shift 3
	set +e
	(cd "$dir" && HEADLESS_TIMEOUT=$timeout_s HEADLESS_SIZE="1024 768" HEADLESS_LOG=$log.out "$TEST_DIR/headless.sh" "$@")
	code=$?
	set -e
	[ "$code" = 124 ] && code="timed out after $timeout_s s"
	plain_log "$log.out" "$log"
	# gamescope 3.16 crashes when the window of the game is gone, and its reaper kills what is left: under Wine
	# that can be the game itself, which ends a moment after its window (137, SIGKILL). The last line of the
	# shutdown of the engine (Z_Shutdown) says that it had ended by itself.
	if [ "$code" = 137 ] && grep -q "Automatically freeing [0-9]* blocks" "$log"; then
		code=0
	fi
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
