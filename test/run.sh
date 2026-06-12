#!/bin/sh
# Headless end-to-end test for mlock.
#
# Runs mlock under Xvfb with an LD_PRELOAD shim that injects a known
# password hash (so no root/setuid is needed), drives it with xdotool and
# checks that a wrong password keeps it locked and the right one unlocks.
#
# Requires: Xvfb, xdotool, openssl. Set MLOCK_TEST_SHOTS=dir to also dump
# screenshots of every state (requires imagemagick).

set -eu
cd "$(dirname "$0")/.."

DPY=:93
PASS=monkey

make >/dev/null
cc -shared -fPIC -o test/shim.so test/shim.c

HASH=$(openssl passwd -6 "$PASS")

Xvfb "$DPY" -screen 0 1280x800x24 -ac >/dev/null 2>&1 &
XVFB_PID=$!
cleanup() {
	[ -n "${MLOCK_PID:-}" ] && kill "$MLOCK_PID" 2>/dev/null
	kill "$XVFB_PID" 2>/dev/null
	wait 2>/dev/null
}
trap cleanup EXIT
sleep 1

shot() {
	[ -n "${MLOCK_TEST_SHOTS:-}" ] || return 0
	mkdir -p "$MLOCK_TEST_SHOTS"
	import -display "$DPY" -window root "$MLOCK_TEST_SHOTS/$1.png"
}

xdo() {
	xdotool "$@" 2>/dev/null
}
export XDOTOOL_DISPLAY="$DPY" DISPLAY="$DPY"

MLOCK_TEST_HASH="$HASH" LD_PRELOAD="$PWD/test/shim.so" ./mlock &
MLOCK_PID=$!
sleep 2

kill -0 "$MLOCK_PID" || { echo "FAIL: mlock died on startup"; exit 1; }
shot 1-init

# typing state (alternating colors)
xdo type --delay 50 "wr"
sleep 0.5
shot 2-typing

# wrong password
xdo type --delay 50 "ong"
xdo key Return
sleep 0.5
shot 3-failed
kill -0 "$MLOCK_PID" || { echo "FAIL: wrong password unlocked the screen"; exit 1; }

# caps lock warning
xdo key Caps_Lock
xdo type --delay 50 "x"
sleep 0.5
shot 4-caps
xdo key BackSpace Caps_Lock

# correct password unlocks
xdo type --delay 50 "$PASS"
xdo key Return

for i in 1 2 3 4 5 6 7 8 9 10; do
	kill -0 "$MLOCK_PID" 2>/dev/null || break
	sleep 0.5
done
if kill -0 "$MLOCK_PID" 2>/dev/null; then
	echo "FAIL: correct password did not unlock"
	exit 1
fi
wait "$MLOCK_PID" 2>/dev/null || { echo "FAIL: mlock exited non-zero"; exit 1; }
MLOCK_PID=

echo "PASS"
