#!/usr/bin/env bash
# Gate: MODEL 3.0's timing contract.
#
# Compiles _diag/anim3_probe.cpp against the real src/anim3_core.h (through src/model.h's seam) and
# checks the properties the model PROMISES:
#   1. one received amount is handed over in EQUAL parts across exactly X ms;
#   2. the total handed over is exactly what came in;
#   3. it is frame-rate independent;
#   4. overlapping windows add up (a roll keeps flowing and builds);
#   5. X is honoured across its whole range (50..400 ms).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="/d/Projects/Code/_tools/w64devkit/bin:$PATH"

OUT="$ROOT/build/_anim3_probe.exe"
g++ -std=c++17 -O2 -I"$ROOT/src" -o "$OUT" "$ROOT/_diag/anim3_probe.cpp"
rc=0
"$OUT" || rc=$?
rm -f "$OUT"
exit $rc
