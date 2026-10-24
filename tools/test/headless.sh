#!/bin/bash
# Runs a program so that no window of it shows up on the desktop and waits until it ends: the program runs in
# gamescope without an output (--backend headless), which gives it a display of its own. DISPLAY and
# WAYLAND_DISPLAY of the desktop are taken away from it, so a program that does not go through gamescope fails
# instead of opening a window. The sound goes nowhere (SDL_AUDIODRIVER=dummy): a test never plays through the
# speakers of whoever sits at the machine. A test that wants its sound names another driver among its own
# VAR=value words, which come later and win (SDL_AUDIODRIVER=disk writes it to a file).
#
# gamescope waits for every process it started, the services Wine leaves behind among them, and 3.16 crashes on
# its way out. So the exit code is the one of the command, a shell inside gamescope writes it into a fifo, and when
# it is there gamescope and whatever is left of the run (its session) are ended.
#
# usage: tools/test/headless.sh [VAR=value ...] <command> [arguments]
#        HEADLESS_TIMEOUT  seconds until the command is killed, default 300. Exit code 124 then.
#        HEADLESS_SIZE     width and height of the display of gamescope, default "1280 720"
#        HEADLESS_LOG      where the output of gamescope and of the command go, default nowhere
timeout_s=${HEADLESS_TIMEOUT:-300}
read -r width height <<< "${HEADLESS_SIZE:-1280 720}"
d=$(mktemp -d)
mkfifo "$d/done"
# shellcheck disable=SC2016 # expanded by the shell inside gamescope
setsid timeout -k 5 "$timeout_s" env -u DISPLAY -u WAYLAND_DISPLAY \
	gamescope --backend headless -W "$width" -H "$height" -w "$width" -h "$height" -- \
	sh -c 'env "$@"; echo $? > "$0"' "$d/done" SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 "$@" \
	> "${HEADLESS_LOG:-/dev/null}" 2>&1 &
session=$!
rc=$(timeout $((timeout_s + 15)) cat "$d/done")
kill -- "-$session" 2> /dev/null
wait "$session" 2> /dev/null
rm -rf "$d"
exit "${rc:-124}"
