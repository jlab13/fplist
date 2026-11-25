#!/usr/bin/env bash
# Smoke test against tools/fakevehicle.c, no hardware and no network:
#
#   tools/ci/smoke.sh [BUILD_DIR]     (default: build)
#
# Downloads the parameters with `fpl --dump` over TCP from the fake vehicle and checks:
#   - ArduPilot over MAVFTP (param.pck), and that the values equal those of PARAM_REQUEST_LIST;
#   - the fallback to PARAM_REQUEST_LIST when the vehicle has no MAVFTP;
#   - both paths with 30% packet loss;
#   - PX4 (byte-wise encoding, PARAM_REQUEST_LIST only).
# Run by CI on every push; an ASan build (-DFPL_ASAN=ON) makes it a memory check too.
set -euo pipefail

BUILD=${1:-build}
FPL=$BUILD/fpl
FAKE=$BUILD/fakevehicle
[ -x "$FPL" ] && [ -x "$FAKE" ] || { echo "smoke: no $FPL or $FAKE: build first" >&2; exit 1; }

WORK=$(mktemp -d)
FAKE_PID=
cleanup() {
	[ -z "$FAKE_PID" ] || kill "$FAKE_PID" 2>/dev/null || true
	rm -rf "$WORK"
}
trap cleanup EXIT

# an empty cache and config: no metadata (and --offline: no download), no favourites
export XDG_CACHE_HOME=$WORK/cache XDG_CONFIG_HOME=$WORK/config LANG=C LC_ALL=C
COUNT=300
PORT=$((20000 + RANDOM % 20000))
FAILED=0

# run <name> <expected stderr text> <fakevehicle options...>: the TSV goes to $WORK/<name>.tsv
run() {
	local name=$1 expect=$2
	shift 2
	PORT=$((PORT + 1))
	"$FAKE" --tcp "$PORT" --count "$COUNT" "$@" >"$WORK/$name.fake.log" 2>&1 &
	FAKE_PID=$!
	sleep 0.3
	local rc=0
	"$FPL" --dump --offline --timeout 60 "tcp:127.0.0.1:$PORT" >"$WORK/$name.tsv" 2>"$WORK/$name.err" || rc=$?
	kill "$FAKE_PID" 2>/dev/null || true
	wait "$FAKE_PID" 2>/dev/null || true
	FAKE_PID=
	local rows
	rows=$(($(wc -l <"$WORK/$name.tsv") - 1)) # minus the header
	if [ $rc -ne 0 ] || [ "$rows" -ne $COUNT ] || ! grep -q "$expect" "$WORK/$name.err"; then
		echo "FAIL $name: exit $rc, $rows/$COUNT parameters, expected '$expect' in:" >&2
		sed 's/^/    /' "$WORK/$name.err" >&2
		FAILED=1
	else
		echo "ok   $name: $rows parameters ($(grep -m1 "$expect" "$WORK/$name.err"))"
	fi
}

# same <a> <b>: both downloads gave the same names and values
same() {
	if cmp -s "$WORK/$1.tsv" "$WORK/$2.tsv"; then
		echo "ok   $1 = $2"
	else
		echo "FAIL $1 != $2:" >&2
		diff "$WORK/$1.tsv" "$WORK/$2.tsv" | head -10 | sed 's/^/    /' >&2
		FAILED=1
	fi
}

run ftp "via MAVFTP"
run list "using PARAM_REQUEST_LIST" --no-ftp
run ftp-loss "via MAVFTP" --loss 30
run list-loss "using PARAM_REQUEST_LIST" --no-ftp --loss 30
run px4 "PX4" --px4
same ftp list
same ftp ftp-loss
same list list-loss

[ $FAILED -eq 0 ] && echo "smoke: all passed" || { echo "smoke: FAILED" >&2; exit 1; }
