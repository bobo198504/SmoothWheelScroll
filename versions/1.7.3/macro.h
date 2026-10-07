#ifndef SWS_MACRO_H
#define SWS_MACRO_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// CUSTOM ACTIONS ("macros") -- parsing the one place their contents live.
//
// REAPER lets a user combine several actions into ONE "Custom:" action. Driving such a macro from
// this plugin needs the LIST of actions inside it, and the SDK does not expose it: its own
// documentation says to find custom action ID strings in reaper-kb.ini, so that file is read. The
// format was measured, not documented:
//
//   ACT 0 0 "c944550409af294391e3382d1bf2964a" "Custom: Mega zoom" 998 991
//   ACT <a> <section> "<guid>" "<name>" <child> <child> ...
//
// A child is either a plain command id ("998") or a named command -- "_SWS_SELNEXTITEM", a ReaScript,
// or ANOTHER custom action (a nested macro; see the note below).
//
// Resolving a child to a command id needs REAPER, so it is deliberately NOT done here: this header
// stops at the tokens, which is the part worth testing outside the host (see
// _diag/macro_parse_probe.cpp). REAPER-free, like routing.h and device.h.
//
// NESTED MACROS NEED NO SPECIAL CASE: a nested child is a "_<guid>" token, and when its id is looked
// up and classified the name rule sees "Custom: ..." -- which fails the rule's "must name a View
// action" test. So a nested macro simply fails classification and the whole outer macro is left
// alone. That is the safe direction, and it happens for free.
// ---------------------------------------------------------------------------

struct MacroDef
{
  static const int kMaxChildren = 8;  // more than this: the macro is treated as unusable
  static const int kMaxToken = 80;

  int section = 0;                  // the KBD section the macro itself lives in
  int nChildren = 0;                // tokens seen after the name
  char child[kMaxChildren][kMaxToken] = {{0}}; // raw child tokens, in order
  bool found = false;               // a matching ACT line was seen
  bool overflow = false;            // the line carried more children than kMaxChildren
};

// Case-insensitive compare, for the guid. REAPER writes them in lower-case hex, but a hand-edited
// file need not, and the cost of being strict here is a feature that silently never works.
inline bool MacroGuidEq(const char *a, const char *b)
{
  if (!a || !b)
    return false;
  for (; *a && *b; ++a, ++b)
  {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb)
      return false;
  }
  return *a == 0 && *b == 0;
}

// Copy the next whitespace-separated token, honouring double quotes (returned without them).
// Returns the position just past the token, or null at end of line.
inline const char *MacroNextToken(const char *p, char *out, int outSize)
{
  if (!p || !out || outSize <= 0)
    return nullptr;
  out[0] = 0;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
    ++p;
  if (!*p)
    return nullptr;
  int n = 0;
  if (*p == '"')
  {
    ++p;
    while (*p && *p != '"')
    {
      if (n < outSize - 1)
        out[n++] = *p;
      ++p;
    }
    if (*p == '"')
      ++p;
  }
  else
  {
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
    {
      if (n < outSize - 1)
        out[n++] = *p;
      ++p;
    }
  }
  out[n] = 0;
  return p;
}

// Parse ONE line. Fills `out` and returns true when the line defines a custom action whose guid
// matches; returns false for anything else (a blank line, a SCR/KEY line, another macro).
inline bool MacroParseLine(const char *line, const char *guid, MacroDef &out)
{
  char tok[MacroDef::kMaxToken];
  const char *p = MacroNextToken(line, tok, (int)sizeof(tok));
  if (!p || strcmp(tok, "ACT") != 0)
    return false;
  p = MacroNextToken(p, tok, (int)sizeof(tok)); // <a>
  if (!p)
    return false;
  p = MacroNextToken(p, tok, (int)sizeof(tok)); // <section>
  if (!p)
    return false;
  const int section = atoi(tok);
  p = MacroNextToken(p, tok, (int)sizeof(tok)); // <guid>
  if (!p || !MacroGuidEq(tok, guid))
    return false;
  p = MacroNextToken(p, tok, (int)sizeof(tok)); // <name>
  if (!p)
    return false;

  out.section = section;
  out.nChildren = 0;
  out.found = true;
  out.overflow = false;
  while (true)
  {
    p = MacroNextToken(p, tok, (int)sizeof(tok));
    if (!p)
      break;
    if (out.nChildren < MacroDef::kMaxChildren)
      _snprintf(out.child[out.nChildren], MacroDef::kMaxToken, "%s", tok);
    else
      out.overflow = true;
    ++out.nChildren;
  }
  return true;
}

// Parse a whole file's text, stopping at the matching ACT line.
inline bool MacroParse(const char *text, const char *guid, MacroDef &out)
{
  out.found = false;
  out.nChildren = 0;
  out.overflow = false;
  if (!text || !guid || !*guid)
    return false;
  const char *p = text;
  while (*p)
  {
    const char *eol = p;
    while (*eol && *eol != '\n')
      ++eol;
    char line[2048];
    int n = (int)(eol - p);
    if (n > (int)sizeof(line) - 1)
      n = (int)sizeof(line) - 1;
    memcpy(line, p, (size_t)n);
    line[n] = 0;
    if (MacroParseLine(line, guid, out))
      return true;
    if (!*eol)
      break;
    p = eol + 1;
  }
  return false;
}

#endif // SWS_MACRO_H
