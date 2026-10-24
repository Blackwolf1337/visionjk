#!/bin/bash
# Static checks: cppcheck over the code of the game repository (codemp and shared, not the libraries of lib/), and
# the scripts of both repositories through shellcheck. The game had many findings of cppcheck when this came,
# they are in tools/test/cppcheck-baseline.txt: only a finding that is not there fails. The scripts have to be clean.
#
# usage: tools/test/lint.sh [options]
#   --baseline     write the findings of cppcheck of now into tools/test/cppcheck-baseline.txt, after a fix or when a
#                  new finding was looked at and is fine
#
# cppcheck reads the compile_commands.json of the game repository (a link to the one of build64) and checks Linux
# x86_64. Its results are kept in build-test/cppcheck, a second run looks only at what changed. All findings of a run:
# build-test/cppcheck.txt. Exit code 0 when nothing new was found.
set -euo pipefail

# shellcheck source=tools/test/test-env.sh
. "$(dirname "${BASH_SOURCE[0]}")/test-env.sh"

BASELINE=$TEST_DIR/cppcheck-baseline.txt
FINDINGS=$WORK/cppcheck.txt

die() {
	echo "$*" >&2
	exit 1
}

usage() {
	sed -n '2,12s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
	exit "${1:-1}"
}

# A finding as the baseline keeps it: file, check and message, without the line, which moves with every change
# above it.
keys() {
	sed "s|^$GAME_REPO/||" "$1" | grep -v '^lib/' | awk -F: '{ line = $2; $2 = ""; print }' OFS=: \
		| sed 's/^\([^:]*\)::/\1:/' | sort -u
}

run_cppcheck() {
	mkdir -p "$WORK/cppcheck"
	[ -f "$GAME_REPO/compile_commands.json" ] || die "$GAME_REPO/compile_commands.json is missing: configure build64 first"
	echo "cppcheck"
	# The compiler defines the macros of the system, cppcheck does not: without them q_platform.h stops every
	# file with its #error.
	(cd "$GAME_REPO" && nice -n "$BUILD_NICE" cppcheck --project=compile_commands.json \
		--file-filter="$GAME_REPO/codemp/*" --file-filter="$GAME_REPO/shared/*" \
		--platform=unix64 -D__linux__=1 -D__x86_64__=1 -D__GNUC__=15 -D__GNUC_MINOR__=0 \
		-j "$BUILD_JOBS" --enable=warning,portability --inline-suppr --quiet \
		--cppcheck-build-dir="$WORK/cppcheck" --template='{file}:{line}:{id}:{severity}:{message}') \
		2> "$FINDINGS" || die "cppcheck failed, see $FINDINGS"
}

run_shellcheck() {
	local scripts=()
	echo "shellcheck"
	# the scripts of this repository, new ones that are not added yet among them, and those of the website
	mapfile -t scripts < <(git -C "$GAME_REPO" ls-files -z -co --exclude-standard '*.sh' \
		| xargs -0 -r -I{} echo "$GAME_REPO/{}"; [ -d "$WEBSITE/tools" ] && find "$WEBSITE/tools" -name '*.sh')
	if ! ( cd "$GAME_REPO" && shellcheck -x "${scripts[@]}" ); then
		echo "FAIL  shellcheck"
		failed=1
	else
		echo "PASS  shellcheck (${#scripts[@]} scripts)"
	fi
}

baseline=0
while [ $# -gt 0 ]; do
	case $1 in
		--baseline) baseline=1; shift ;;
		-h|--help) usage 0 ;;
		*) usage ;;
	esac
done
for tool in cppcheck shellcheck; do
	command -v "$tool" > /dev/null || die "$tool is missing"
done
failed=0

run_cppcheck
if [ "$baseline" = 1 ]; then
	{
		echo "# Findings of tools/test/lint.sh (cppcheck) that are known, as file:check:severity:message."
		echo "# Written by tools/test/lint.sh --baseline, do not edit by hand."
		keys "$FINDINGS"
	} > "$BASELINE"
	echo "$(grep -vc '^#' "$BASELINE") findings written to $BASELINE"
else
	[ -f "$BASELINE" ] || die "$BASELINE is missing: tools/test/lint.sh --baseline writes it"
	new=$(comm -23 <(keys "$FINDINGS") <(grep -v '^#' "$BASELINE" | sort -u))
	gone=$(comm -13 <(keys "$FINDINGS") <(grep -v '^#' "$BASELINE" | sort -u) | wc -l)
	if [ -n "$new" ]; then
		echo "FAIL  cppcheck: new findings (the lines are in $FINDINGS):"
		printf '%s\n' "$new"
		failed=1
	else
		echo "PASS  cppcheck (no new finding)"
		[ "$gone" = 0 ] || echo "      $gone findings of the baseline are gone, tools/test/lint.sh --baseline takes them out"
	fi
fi
run_shellcheck
exit "$failed"
