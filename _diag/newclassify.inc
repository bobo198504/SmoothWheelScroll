// Name-based classification: the thin REAPER-facing shell around routing.h.
//
// The rule itself (which names are admitted, and the axis/kind they resolve to) lives in
// ClassifyName() in src/routing.h -- pure string logic with no REAPER dependency, so it can be
// compiled and diffed outside REAPER. This function only fetches the name from REAPER and hands it
// over, which is the one part that cannot leave this file.
static bool ClassifyByName(KbdSectionInfo *sec, int command, ActionSpec &out)
{
  if (!sec || !kbd_getTextFromCmd)
    return false;
  const char *nm = kbd_getTextFromCmd(command, sec);
  if (!ClassifyName(sec->uniqueID, nm, out))
    return false;
  out.command = command; // the name rule does not carry the id; the caller has it
  return true;
}
