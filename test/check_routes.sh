#!/usr/bin/env bash
# Gate: the ROUTING rule must reproduce the frozen 1.6.1 behaviour, except for RECORDED changes.
#
# It compiles _diag/route_probe.cpp, which includes the REAL src/routing.h (not a hand-copied
# mirror), runs it, and compares its output against test/expected/routes_before.txt.
#
# That frozen file was produced BEFORE the unification by _diag/legacy_rules.cpp, a faithful
# transcription of 1.6.1's three decision sites. So a diff proves the unification -- table, name
# rule and filter, wherever they are called from -- changed no decision.
#
# THE RECORDED CHANGE:
#   (104) the MAIN view's HORIZONTAL PAN (988/977) moved stream -> step: a pixel pan DISCARDS
#         sub-unit pieces, so a slow notch moved nothing until a piece reached a whole unit.
#   (126) the `rel` column moved 0 -> 1 on EVERY table row: the table now DECLARES that its actions
#         are "(MIDI CC relative/mousewheel)" ones instead of leaving them at the default false and
#         letting the name rule answer differently for the very same action. The table is tried
#         first, so its `false` decided, and the horizontal actions -- which a tilt wheel / thumb
#         wheel drives -- demanded a wheel latch that a horizontal wheel message could never arm.
#         Only the `rel` column may move: axis, drive and delivery must stay EXACTLY as they were.
#
# (Two more delivery changes were tried and REVERTED, so they are NOT in the allowed set: the MIDI
# horizontal pan whole-unit and synthetic-wheel attempts -- AGENTS.md 105/106/107 -- and the MAIN
# vertical ZOOM going to the fine stream -- AGENTS.md 109, reverted in 110 because a fine grid makes
# that axis jitter and a slow zoom not move. Both are back to their 1.6.1 form.)
#
# The gate does NOT simply accept the new file (that is how the rule drifted silently before,
# AGENTS.md 70). It asserts the ONLY changed lines are those recordings -- so any OTHER change still
# fails: a changed vertical id, a changed MIDI row, a new name rule, anything.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="/d/Projects/Code/_tools/w64devkit/bin:$PATH"

OUT="$ROOT/build/_route_probe.exe"
g++ -std=c++17 -O2 -I"$ROOT/src" -o "$OUT" "$ROOT/_diag/route_probe.cpp"
"$OUT" > "$ROOT/build/_routes_after.txt"

# The horizontal wheel (a tilt wheel / thumb wheel) must be wired everywhere the vertical one is:
# the table and the name rule have to AGREE about relativeAction, and the message hook has to watch
# WM_MOUSEHWHEEL. Without both, the action a thumb wheel drives demands a latch nothing can arm.
g++ -std=c++17 -O2 -I"$ROOT/src" -o "$ROOT/build/_hwheel_probe.exe" "$ROOT/_diag/hwheel_probe.cpp"
rc_rel=0
"$ROOT/build/_hwheel_probe.exe" || rc_rel=1

# THE TWO BEHAVIOURS OF 1.7.4 that are pure arithmetic and would otherwise be unguarded:
#   * topup_dir  -- the end-of-gesture top-up must fire WITH the gesture, never against it (the
#                   "scrolls then bounces back" bug). The probe carries the captured reversal.
#   * rowgain    -- the row-height compensation: the reference end must stay exactly 1.0 and the
#                   ceiling must be kRowGainBase * 1.2, with the reference MEASURED (196px) rather
#                   than guessed -- the first two attempts failed on exactly that.
for p in topup_dir_probe rowgain_probe; do
  g++ -std=c++17 -O2 -I"$ROOT/src" -o "$ROOT/build/_${p}.exe" "$ROOT/_diag/${p}.cpp"
  "$ROOT/build/_${p}.exe" || rc_rel=1
done

ALLOWED_ADD='\| +(988|977) \|.* ?step'
# A REMOVED line may be either half of the two recordings: the old horizontal pan (stream, 988/977)
# or ANY table row before it declared itself relative (the `0` in the rel column). Both are
# superseded lines, and nothing else may disappear.
ALLOWED_DEL='\|( +(988|977) \|.*stream|.*yes +replay +[HV] +0 +)'
# The recorded `rel` change (126): the line differs ONLY in that column.
ALLOWED_REL='\|.*yes +replay +[HV] +1 +'

rc=0
while IFS= read -r line; do
  case "$line" in
    '+++'*|'---'*) continue ;;
    '+'*) printf '%s\n' "$line" | grep -qE "$ALLOWED_ADD" ||
          printf '%s\n' "$line" | grep -qE "$ALLOWED_REL" || {
           echo "UNEXPECTED ADDED LINE: $line"; rc=1; } ;;
    '-'*) printf '%s\n' "$line" | grep -qE "$ALLOWED_DEL" || {
           echo "UNEXPECTED REMOVED LINE: $line"; rc=1; } ;;
  esac
done < <(diff "$ROOT/test/expected/routes_before.txt" "$ROOT/build/_routes_after.txt" || true)

# And the recorded changes must actually BE there (guards against a silent revert).
grep -qE '\| +(988|977) \|.* ?step' "$ROOT/build/_routes_after.txt" || {
  echo "MISSING: the main horizontal pan is no longer whole units"; rc=1; }
# Every table row must now declare itself a relative action: the corpus' table-matched rows are the
# ones with a real id (< 40000) in the main/MIDI sections. A single `0` left behind is the bug.
if grep -E '^\s*(0|100|32060) \| +(988|977|989|978|990|979|1000|1001|40430|40431|40432|40433|40660|40661|40662|40663) \|' \
     "$ROOT/build/_routes_after.txt" | grep -qE '\| +0 +'; then
  echo "MISSING: a table row still reports relativeAction = 0 (the name rule would say 1)"; rc=1
fi

# THE STRONG FORM OF THE rel RECORDING: with the rel column blanked out, the two files must be
# IDENTICAL apart from the horizontal pan's step. Allowing a line merely because it "has rel = 1"
# would also accept a line whose axis or delivery changed at the same time; this compares everything
# else, so only the rel column (and the pan) may differ.
NORM='s/(yes|REFUSE) +replay +([HV]) +[01] +([a-z]+)/\1     replay \2   \3/'
sed -E "$NORM" "$ROOT/test/expected/routes_before.txt" > "$ROOT/build/_rel_norm_before.txt"
sed -E "$NORM" "$ROOT/build/_routes_after.txt" > "$ROOT/build/_rel_norm_after.txt"
while IFS= read -r line; do
  case "$line" in
    '+++'*|'---'*) continue ;;
    '+'*|'-'*) printf '%s\n' "$line" | grep -qE '\| +(988|977) \|.*(stream| ?step)' || {
      echo "UNEXPECTED NON-rel CHANGE: $line"; rc=1; } ;;
  esac
done < <(diff "$ROOT/build/_rel_norm_before.txt" "$ROOT/build/_rel_norm_after.txt" || true)

rm -f "$OUT" "$ROOT/build/_routes_after.txt" "$ROOT/build/_hwheel_probe.exe" \
      "$ROOT/build/_topup_dir_probe.exe" "$ROOT/build/_rowgain_probe.exe" \
      "$ROOT/build/_rel_norm_before.txt" "$ROOT/build/_rel_norm_after.txt"
rc=$((rc + rc_rel))
if [ $rc -ne 0 ]; then
  echo ""
  echo "FAIL: routing differs from 1.6.1 by more than the recorded changes."
  exit 1
fi
echo ""
echo "OK: routing reproduces 1.6.1 except the recorded changes (104: main pan step, 126: table rel);"
echo "    the horizontal wheel is wired (hook watches WM_MOUSEHWHEEL, table agrees with the name rule)."
