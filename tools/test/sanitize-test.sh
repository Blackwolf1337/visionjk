#!/bin/bash
# Builds the game of the game repository as it is (HEAD and changes) for Linux on this machine with
# AddressSanitizer and UndefinedBehaviorSanitizer, runs its unit tests (tests/, Boost) and tools/test/smoke-test.sh on
# it, then builds the dedicated server and the server-side gamecode without them and runs the server once under
# valgrind. A memory error or undefined behaviour in any of those runs fails it. Nothing is published, nothing of
# the game repository but its build folders changes.
#
# usage: tools/test/sanitize-test.sh [options]
#   --only sanitizers|valgrind   one of the two parts (the unit tests are part of sanitizers)
#   --keep                       leave build-test/smoke as it is after the run
#
# Build folders: build-sanitize and build-valgrind in the game repository, the installs in build-test/stage-sanitize
# and build-test/stage-valgrind. Logs: build-test/logs/sanitize-*.log (the unit tests: sanitize-unit.log), the smoke test
# in build-test/smoke, valgrind in build-test/smoke/valgrind-server.*. Exit code 0 when everything passed.
set -euo pipefail

# shellcheck source=tools/test/test-env.sh
. "$(dirname "${BASH_SOURCE[0]}")/test-env.sh"

LOGS=$WORK/logs
MAP=mp/ffa1
BOT=Desann
# valgrind makes the server some 30 times slower
TIMEOUT_VALGRIND=900

die() {
	echo "$*" >&2
	exit 1
}

usage() {
	sed -n '2,14s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
	exit "${1:-1}"
}

want() {
	[ -z "$only" ] || [ "$only" = "$1" ]
}

# configures, builds and installs one build folder; the arguments after the first two go to cmake
build() {
	local name=$1 stage=$2 build=$GAME_REPO/build-$1
	shift 2
	echo "building $name"
	cmake -S "$GAME_REPO" -B "$build" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBuildMasterServer=OFF \
		-DCMAKE_C_FLAGS="-fno-omit-frame-pointer" -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer" \
		-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
		-DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold" -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=mold" \
		-DSteamAudioSDK="$STEAMAUDIO_SDK" -DFetchSteamAudio=OFF "$@" > "$LOGS/sanitize-$name-configure.log" 2>&1 \
		|| die "configure of $name failed, see $LOGS/sanitize-$name-configure.log"
	nice -n "$BUILD_NICE" cmake --build "$build" -j "$BUILD_JOBS" > "$LOGS/sanitize-$name-build.log" 2>&1 \
		|| die "build of $name failed, see $LOGS/sanitize-$name-build.log"
	rm -rf "$stage"
	cmake --install "$build" --prefix "$stage" > "$LOGS/sanitize-$name-install.log" 2>&1 \
		|| die "install of $name failed, see $LOGS/sanitize-$name-install.log"
}

# prints the folder of an install that holds the executable
game_dir() {
	local found
	found=$(find "$1" -type f -name "$2")
	[ -n "$found" ] && [ "$(printf '%s\n' "$found" | wc -l)" = 1 ] || die "$1 does not have exactly one $2"
	dirname "$found"
}

test_sanitizers() {
	local stage=$WORK/stage-sanitize game args=() log=$LOGS/sanitize-unit.log
	build sanitize "$stage" -DUseAddressSanitizer=ON -DUseUndefinedSanitizer=ON -DBuildTests=ON

	# the unit tests, with the sanitizers on the code they look at
	if ASAN_OPTIONS=detect_leaks=1 \
		UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1:suppressions=$TEST_DIR/sanitize-suppressions.txt" \
		"$GAME_REPO/build-sanitize/UnitTests" --log_level=error --color_output=no > "$log" 2>&1; then
		echo "PASS  unit tests ($(grep -m 1 -o 'Running [0-9]* test cases' "$log" | grep -o '[0-9]*') cases)"
	else
		echo "FAIL  unit tests, see $log"
		grep -m 5 -E 'error|ERROR' "$log" || true
		failed=1
	fi

	game=$(game_dir "$stage" visionjk.x86_64)
	[ "$keep" = 1 ] && args+=(--keep)
	"$TEST_DIR/smoke-test.sh" "$version" --dir "$game" "${args[@]}" || failed=1
}

# The server loads the level, takes a bot and quits under valgrind. Errors fail the run; leaks are only counted,
# the engine keeps its hunks to the end on purpose.
test_valgrind() {
	local stage=$WORK/stage-valgrind game home=$WORK/smoke/home-valgrind-server log=$WORK/smoke/valgrind-server
	local code pk3 errors
	command -v valgrind > /dev/null || die "valgrind is missing"
	build valgrind "$stage" -DBuildMPEngine=OFF -DBuildMPRdVanilla=OFF -DBuildMPRdVulkanVision=OFF \
		-DBuildMPCGame=OFF -DBuildMPUI=OFF -DBuildJPAssets=OFF -DBuildJPSpatialAudio=OFF
	game=$(game_dir "$stage" visionjkded.x86_64)
	for pk3 in "$BASE_ASSETS"/assets*.pk3; do
		ln -sf "$pk3" "$game/base/$(basename "$pk3")"
	done
	rm -rf "$home"
	mkdir -p "$home"
	echo "valgrind: dedicated server"
	set +e
	(cd "$game" && timeout -k 10 "$TIMEOUT_VALGRIND" valgrind --error-exitcode=99 --track-origins=yes \
		--leak-check=full --errors-for-leak-kinds=none --log-file="$log.valgrind" \
		./visionjkded.x86_64 +set fs_homepath "$home" +set dedicated 1 +set net_port 29098 +set bot_enable 1 \
		+map "$MAP" +wait 100 +addbot "$BOT" +wait 400 +quit) > "$log.log" 2>&1
	code=$?
	set -e
	errors=$(grep -m 1 -o 'ERROR SUMMARY: [0-9]*' "$log.valgrind" | grep -o '[0-9]*$' || echo "?")
	if [ "$code" = 0 ] && [ "$errors" = 0 ] && grep -q "ClientBegin: 0" "$log.log"; then
		echo "PASS  valgrind dedicated server ($(grep -m 1 'definitely lost:' "$log.valgrind" | sed 's/^==[0-9]*== *//'))"
	else
		echo "FAIL  valgrind dedicated server: exit code $code, $errors errors, see $log.valgrind"
		failed=1
	fi
}

only=
keep=0
while [ $# -gt 0 ]; do
	case $1 in
		--only) only=${2:-}; shift 2 ;;
		--keep) keep=1; shift ;;
		-h|--help) usage 0 ;;
		*) usage ;;
	esac
done
case $only in ''|sanitizers|valgrind) ;; *) usage ;; esac
[ -f "$BASE_ASSETS/assets0.pk3" ] || die "The base assets are not in $BASE_ASSETS"
[ -d "$STEAMAUDIO_SDK" ] || die "The Steam Audio SDK is not in $STEAMAUDIO_SDK"
mkdir -p "$LOGS" "$WORK/smoke"

# what the game says it is: git describe of the game repository, as the build takes it
version=$(git -C "$GAME_REPO" describe --tag)
version=${version#v}
failed=0

want sanitizers && test_sanitizers
want valgrind && test_valgrind
exit "$failed"
