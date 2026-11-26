#!/bin/sh
# Headless end-to-end test for lok.
#
# Runs lok under Xvfb with an LD_PRELOAD shim that injects a known
# password hash (so no root/setuid is needed), drives it with xdotool and
# checks that a wrong password keeps it locked and the right one unlocks.
#
# Requires: Xvfb, xdotool, openssl. Set LOK_TEST_SHOTS=dir to also dump
# screenshots of every state (requires imagemagick).

set -eu
cd "$(dirname "$0")/.."

DPY=:93
PASS=monkey

make >/dev/null
cc -shared -fPIC -o test/shim.so test/shim.c

HASH=$(openssl passwd -6 "$PASS")
CONFIG_TMP=$(mktemp -d)
export XDG_CONFIG_HOME="$CONFIG_TMP"
mkdir "$CONFIG_TMP/lok"
cat > "$CONFIG_TMP/lok/config.ini" <<'EOF'
[colors]
idle = #123456
failed = not-an-x11-color
[text]
failed_attempt = Attempt %d %% %n
EOF

Xvfb "$DPY" -screen 0 1280x800x24 -ac >/dev/null 2>&1 &
XVFB_PID=$!
cleanup() {
	if [ -n "${LOK_PID:-}" ]; then kill "$LOK_PID" 2>/dev/null || true; fi
	kill "$XVFB_PID" 2>/dev/null || true
	wait 2>/dev/null || true
	rm -r "$CONFIG_TMP"
}
trap cleanup EXIT
sleep 1

shot() {
	[ -n "${LOK_TEST_SHOTS:-}" ] || return 0
	mkdir -p "$LOK_TEST_SHOTS"
	import -display "$DPY" -window root "$LOK_TEST_SHOTS/$1.png"
}

xdo() {
	xdotool "$@" 2>/dev/null
}
export XDOTOOL_DISPLAY="$DPY" DISPLAY="$DPY"

LOK_TEST_HASH="$HASH" LD_PRELOAD="$PWD/test/shim.so" ./lok 2> "$CONFIG_TMP/lok.log" &
LOK_PID=$!
sleep 2

kill -0 "$LOK_PID" || { cat "$CONFIG_TMP/lok.log"; echo "FAIL: lok died on startup"; exit 1; }
grep -Fq 'invalid [colors] failed in config.ini' "$CONFIG_TMP/lok.log"
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
kill -0 "$LOK_PID" || { echo "FAIL: wrong password unlocked the screen"; exit 1; }

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
	kill -0 "$LOK_PID" 2>/dev/null || break
	sleep 0.5
done
if kill -0 "$LOK_PID" 2>/dev/null; then
	echo "FAIL: correct password did not unlock"
	exit 1
fi
wait "$LOK_PID" 2>/dev/null || { echo "FAIL: lok exited non-zero"; exit 1; }
LOK_PID=
cat "$CONFIG_TMP/lok.log"

echo "PASS"
