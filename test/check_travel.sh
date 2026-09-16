#!/usr/bin/env bash
# Gate: the model's TRAVEL -- how far a notch goes, from the speed budget.
#
#   travel = start + (cap - start) * min(budget / budgetDeltas, 1),   cap = this message's own deltas
#
# Pins: the endpoints, that the cap is the MESSAGE's size (not a fixed 120), that the budget follows
# the current speed (decays), and -- the whole point -- that a notched mouse and a free-spinner hand
# over nearly the same travel for the same hand movement.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="/d/Projects/Code/_tools/w64devkit/bin:$PATH"

OUT="$ROOT/build/_travel_probe.exe"
g++ -std=c++17 -O2 -I"$ROOT/src" -o "$OUT" "$ROOT/_diag/travel_budget_probe.cpp"
rc=0
"$OUT" || rc=$?
rm -f "$OUT"
exit $rc
