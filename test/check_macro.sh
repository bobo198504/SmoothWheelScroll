#!/usr/bin/env bash
# Gate: the "Custom:" macro path, END TO END.
#
#   1. _diag/macro_parse_probe.cpp -- the ACT line parser over lines copied from reaper-kb.ini.
#   2. _diag/macro_gate_probe.cpp  -- which children the macro rule accepts and, more importantly,
#      which it MUST refuse: a "one page" scroll (jumps a whole page per call, ignores the value
#      handed in -- AGENTS.md 19.8), a script, a nested macro, a plain non-wheel "View:" action.
#      Accepting any of those would animate something that must run once.
#   3. _diag/macro_chain_probe.cpp -- the whole chain: ACT line -> children -> one notch split across
#      them, asserting every child is reached and each gets the full travel. This is the check that a
#      cross-axis macro (zoom both ways -- the common shape) works.
#
# All three include the REAL src/macro.h and src/routing.h, not copies.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="/d/Projects/Code/_tools/w64devkit/bin:$PATH"

rc=0
for p in macro_parse_probe macro_gate_probe macro_chain_probe; do
  OUT="$ROOT/build/_${p}.exe"
  g++ -std=c++17 -O2 -I"$ROOT/src" -o "$OUT" "$ROOT/_diag/${p}.cpp"
  "$OUT" || rc=1
  rm -f "$OUT"
done
exit $rc
