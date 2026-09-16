#!/usr/bin/env bash
# Gate: the model is CONSERVATIVE -- take N, give N.
#
# The eat (Resistance) and the spit (Accel) are gone, so the model's only job is to spread each amount
# over `precisionMs`. This gate pins the one promise that leaves: the total handed over equals the
# total fed, exactly, at any speed and any window -- and each window still ends at exactly windowMs.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="/d/Projects/Code/_tools/w64devkit/bin:$PATH"

OUT="$ROOT/build/_conserve_probe.exe"
g++ -std=c++17 -O2 -I"$ROOT/src" -o "$OUT" "$ROOT/_diag/conservation_probe.cpp"
rc=0
"$OUT" || rc=$?
rm -f "$OUT"
exit $rc
