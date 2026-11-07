#!/bin/bash
# Runs the tests of a release at the same time instead of one after the other, waits for all of them and says
# which passed. Each one keeps its own folder and port (smoke-test.sh of the release in build-test/smoke, the one of
# sanitize-test.sh in build-test/smoke-sanitize, the updater on Linux on 8099 and on Windows on 8098), so they do
# not meet. The time of a run is about the one of its longest test (the updater under Wine, some five minutes).
#
# usage: tools/test/all.sh <version> [options]
#   <version>      the version whose zips are in VJK_DOWNLOADS (test-env.sh), for example 0.2.7
#   --compat       also compat-test.sh: when the protocol, the connect or the JA+ parts changed
#   --updater      also the updater end to end on Linux and Windows: when the updater or the files of a release
#                  changed
#   --full         both
#
# Always: smoke-test.sh, sanitize-test.sh (with the unit tests and valgrind), lint.sh, and tools/test-site.sh of the
# website repository (what build-game.sh puts into the site for a release and a development build). The output of
# each is in build-test/all/<test>.log, this prints their PASS and FAIL lines. Exit code 0 when every test passed.
set -euo pipefail

# shellcheck source=tools/test/test-env.sh
. "$(dirname "${BASH_SOURCE[0]}")/test-env.sh"

ALL=$WORK/all

usage() {
	sed -n '2,16s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
	exit "${1:-1}"
}

version=
compat=0
updater=0
while [ $# -gt 0 ]; do
	case $1 in
		--compat) compat=1; shift ;;
		--updater) updater=1; shift ;;
		--full) compat=1; updater=1; shift ;;
		-h|--help) usage 0 ;;
		-*) usage ;;
		*) [ -z "$version" ] || usage; version=$1; shift ;;
	esac
done
[ -n "$version" ] || usage

rm -rf "$ALL"
mkdir -p "$ALL"
names=()
pids=()

# start <name> <command> [arguments]: one test in the background, its output into its log
start() {
	local name=$1
	shift
	"$@" > "$ALL/$name.log" 2>&1 &
	names+=("$name")
	pids+=($!)
}

start smoke "$TEST_DIR/smoke-test.sh" "$version"
start sanitize "$TEST_DIR/sanitize-test.sh"
start lint "$TEST_DIR/lint.sh"
start site "$WEBSITE/tools/test-site.sh"
if [ "$compat" = 1 ]; then
	start compat "$TEST_DIR/compat-test.sh" "$version"
fi
if [ "$updater" = 1 ]; then
	start updater-linux "$TEST_DIR/updater/e2e.sh"
	start updater-windows env WIN=1 "$TEST_DIR/updater/e2e.sh"
fi
echo "running: ${names[*]} (logs in $ALL)"

failed=0
for i in "${!pids[@]}"; do
	if wait "${pids[$i]}"; then
		code=0
	else
		code=$?
	fi
	echo "== ${names[$i]}: $([ "$code" = 0 ] && echo passed || echo "FAILED (exit code $code)")"
	grep -E '^(PASS|FAIL)  |FAILED|checks, ' "$ALL/${names[$i]}.log" | sed 's/^/   /' || true
	[ "$code" = 0 ] || failed=1
done
exit "$failed"
