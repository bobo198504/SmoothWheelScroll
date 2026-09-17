#!/usr/bin/env bash
# Gate: the DEV wheel log's ring reads in the right ORDER, and its file says what it should.
#
# It compiles _diag/wheel_log_probe.cpp, which includes the REAL src/wheel_log.h (not a copy of
# it), and runs it. The ring's order is the whole value of the file -- a report that reads backwards
# wastes the tester's round trip -- and that is exactly the kind of thing that cannot be seen by
# eye in a text file, so it is asserted here.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="/d/Projects/Code/_tools/w64devkit/bin:$PATH"

OUT="$ROOT/build/_wheel_log_probe.exe"
g++ -std=c++17 -O2 -I"$ROOT/src" -o "$OUT" "$ROOT/_diag/wheel_log_probe.cpp"
( cd "$ROOT/build" && "$OUT" )
rc=$?
rm -f "$OUT" "$ROOT/build/wl_probe_out.txt" "$ROOT/build/wl_probe_out.tmp"
exit $rc
