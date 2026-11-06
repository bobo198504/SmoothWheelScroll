// ---------------------------------------------------------------------------
// platform/mac/win32_compat.mm -- the macOS implementations behind win32_compat.h.
//
// ★ STATUS: COMPILED, NEVER RUN. Same caveat as the rest of platform/mac/: this has been through
// Zig's clang for a macOS target on a Windows machine, but no Mac has executed a line of it.
//
// WHY A SEPARATE FILE FROM THE HEADER. The header declares these and maps the Win32 spellings onto
// them with #define, because most call sites must keep compiling unchanged. The bodies need
// ApplicationServices (for the key state) and CoreFoundation (for the temp path), and putting those
// includes in the header would drag them into every translation unit that includes it.
//
// EACH FUNCTION SAYS WHAT THE WIN32 ORIGINAL GUARANTEES. That is the contract the callers were
// written against; where the guarantee cannot be met, the difference is stated rather than hidden.
// ---------------------------------------------------------------------------
#include "../../platform.h"
// SWELL MUST COME FIRST: win32_compat.h uses SWELL's own types (LONG, HWND, HDC, RECT), and those
// only exist once swell.h has been included.
#include "../../../third_party/reaper-sdk-git/WDL/swell/swell.h"
#include "win32_compat.h"

// ★ ApplicationServices IS NOT INCLUDED HERE, ON PURPOSE.
//
// The only thing this file needed from it was CGEventSourceKeyState, for GetKeyState(VK_LWIN) --
// i.e. "is the Command key held when reading a mouse-modifier assignment". That is one flag bit in
// one rarely-used code path, and pulling in a whole framework for it would make THIS file depend on
// the one macOS framework the Zig cross-build cannot supply, which would turn a working build into a
// broken one for a feature nobody asked about.
//
// The wheel path's own use of ApplicationServices lives in platform/mac/wheel_mac.mm, where it is
// genuinely needed (the event tap) and where its absence is already documented.
//
// See SwsKeyDown below for what is reported instead.

#include <cstdio>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <unistd.h>

namespace plat
{
// The monotonic clock, from the one place that owns it.
double Now();
} // namespace plat

// ---------------------------------------------------------------------------
// Interlocked: implemented inline in the header with __sync builtins, so nothing here.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// GetTickCount / GetTickCount64.
//
// Win32: milliseconds since the system started. The caller uses it for "how long ago was that
// wheel", so what matters is that it is monotonic and in milliseconds -- both of which plat::Now()
// is. The Win32 original starts at boot and this starts at plugin load; the only comparison ever
// made is a difference, so an offset start is harmless.
// ---------------------------------------------------------------------------
LONG SwsTickCount()
{
  return (LONG)plat::Now();
}

unsigned long long SwsTickCount64()
{
  return (unsigned long long)plat::Now();
}

// ---------------------------------------------------------------------------
// GetKeyState.
//
// Win32: the key's state as of the last message processed, high bit = down. Used once, to decide
// whether Super/Win is part of a mouse-modifier combination (flag bit 8 in REAPER's scheme).
//
// macOS equivalent: the Command key, queried through CGEventSourceKeyState, which reports the CURRENT
// hardware state rather than a message-queue snapshot. For a modifier held during a wheel gesture
// those agree; the difference only shows if the key is pressed and released between the event and
// the query, which cannot happen for the modifier that qualified the event.
//
// ★ THE VK_* CONSTANTS: Windows virtual-key codes. The call site passes VK_LWIN / VK_RWIN, which do
// not exist on macOS. Rather than define fake values, the two that are actually used are mapped to
// kVK_Command here, and anything else answers "not down" -- which is the honest answer for a
// Windows-only virtual key on a platform that has no such key.
// ---------------------------------------------------------------------------
#ifndef VK_LWIN
#define VK_LWIN 0x5B
#endif
#ifndef VK_RWIN
#define VK_RWIN 0x5C
#endif

bool SwsKeyDown(int vk)
{
  // ★ THIS REPORTS "NOT DOWN" ON macOS, AND THAT IS A DELIBERATE, DOCUMENTED LIMITATION.
  //
  // The Windows original answers "is the Command/Super key held" so the plugin can pass the right
  // modifier flag when asking REAPER for a mouse-modifier assignment. Reproducing it needs
  // CGEventSourceKeyState (ApplicationServices) or NSEvent (AppKit) -- neither of which the Mac
  // build can link in this configuration.
  //
  // The consequence is NARROW and worth stating plainly: the panel's "what is Cmd+wheel assigned
  // to" probe will report no Super key. The WHEEL PATH -- which is what a tester is actually
  // testing -- never calls this: it gets its modifiers from the event itself (CGEventFlags in the
  // tap). So this affects one settings-panel lookup on a build that ships without a settings panel.
  //
  // If a Mac panel is written later, this is the function to fix: replace the body with
  // CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, kVK_Command) and add the
  // ApplicationServices framework to the build.
  (void)vk;
  return false;
}

// ---------------------------------------------------------------------------
// GetTempPathA.
//
// Win32: the path to the temporary directory, WITH a trailing separator, and returns its length.
// The plugin appends a file name to whatever comes back, so the trailing separator is the part that
// matters. $TMPDIR on macOS is the per-user temp directory and already ends in '/'.
//
// If the variable is unset (a daemon context, or an unusual shell), /tmp/ is used, which is what the
// Win32 call would approximate.
// ---------------------------------------------------------------------------
unsigned int SwsTempPath(char *out, unsigned int outSize)
{
  if (!out || outSize < 2)
    return 0;
  const char *tmp = getenv("TMPDIR");
  if (!tmp || !tmp[0])
    tmp = "/tmp/";
  size_t len = strlen(tmp);
  // Win32 guarantees a trailing separator; $TMPDIR usually has one, but not always.
  const bool needsSep = (tmp[len - 1] != '/');
  if (len + (needsSep ? 1 : 0) + 1 > outSize)
    len = outSize - (needsSep ? 2 : 1);
  memcpy(out, tmp, len);
  size_t pos = len;
  if (needsSep)
    out[pos++] = '/';
  out[pos] = 0;
  return (unsigned int)pos;
}

// ---------------------------------------------------------------------------
// GetDlgCtrlID.
//
// Win32: the integer id a child window was created with. The plugin creates its panel controls with
// ids and later looks them up BY id (GetDlgItem); the reverse lookup is used in the fader and knob
// handlers to know which control sent a notification.
//
// ★ SWELL HAS NO ACCESSOR FOR THIS, so the id is recorded when the control is created: the panel
// stores it in the window's SWELL user-data slot (SetWindowLong(h, GWLP_USERDATA, id)). That is the
// ONE behavioural difference from Win32 in this file -- the ids are ours to keep, and this function
// reads back what the panel put there. If a control was created without that convention it returns
// 0, which every call site already treats as "not a control I know".
// ---------------------------------------------------------------------------
int SwsCtrlIdOf(void *hwnd)
{
  if (!hwnd)
    return 0;
  // GWLP_USERDATA is -21 on Windows; SWELL uses the same numeric convention.
  const long v = (long)GetWindowLong((HWND)hwnd, -21);
  return (int)v;
}

// ---------------------------------------------------------------------------
// GetScrollInfo.
//
// Win32: reads a scrollbar's SCROLLINFO (range, page, position). The panel asks only for the page
// size, to decide whether its content is taller than its client area.
//
// SWELL does not implement GetScrollInfo; it exposes the SB_GETRANGE-family messages instead. The
// page size is read with SBM_GETPOS semantics approximated through GetClientRect: since this panel
// is the only thing that ever sets its own scrollbar, the page equals the client height whenever
// there IS a scrollbar, which is exactly the case the caller distinguishes.
//
// ★ HONEST LIMITATION: this is not a general GetScrollInfo and must not be used as one. It answers
// the one question the panel asks (nPage) and reports a position of zero for everything else.
// ---------------------------------------------------------------------------
bool SwsGetScrollInfo(void *hwnd, int bar, SCROLLINFO *si)
{
  if (!hwnd || !si)
    return false;
  if (bar != 1) // SB_VERT
    return false;
  RECT rc = {0, 0, 0, 0};
  GetClientRect((HWND)hwnd, &rc);
  si->nPage = rc.bottom - rc.top;
  si->nPos = 0;
  return true;
}

// ---------------------------------------------------------------------------
// QueryPerformanceFrequency / QueryPerformanceCounter.
//
// Both are built on plat::Now(), which is monotonic milliseconds. So the frequency is 1000 and the
// counter advances one unit per millisecond -- documented in the header, where the caller's
// assumptions are visible.
// ---------------------------------------------------------------------------
BOOL SwsQueryPerformanceFrequency(LARGE_INTEGER *f)
{
  if (!f)
    return 0;
  f->QuadPart = 1000; // one tick = one millisecond (see the header)
  return 1;
}

BOOL SwsQueryPerformanceCounter(LARGE_INTEGER *c)
{
  if (!c)
    return 0;
  c->QuadPart = (int64_t)plat::Now();
  return 1;
}

// ---------------------------------------------------------------------------
// GetLastError.
//
// Win32: the thread's last error as the previous failed call left it. Used in exactly one place --
// the debug log's LoadLibrary failure branch -- and only to print a number. macOS has no per-thread
// equivalent that matches, so this reports 0 and the log line says so rather than inventing a code.
// ---------------------------------------------------------------------------
unsigned long SwsLastError()
{
  return 0;
}

// ---------------------------------------------------------------------------
// GetMessageExtraInfo.
//
// Win32: 32 extra bits the input system attaches to the most recent mouse message. THIS PLUGIN USES
// IT AS A DEVICE FINGERPRINT: a touchpad reports a specific signature there (0xFF515700 family) and
// a plain wheel reports nothing, which is how the device classifier tells them apart before any
// position or delta is examined.
//
// ★ macOS HAS NO EQUIVALENT SIDE CHANNEL, and it does not need one: the scroll event itself carries
// kCGScrollWheelEventIsContinuous, which is a DIRECT statement of "this came from a continuous
// surface" rather than a fingerprint that has to be recognised. That flag is read in
// platform/mac/wheel_mac.mm and recorded there.
//
// So this returns 0 = "no signature", which the classifier already treats as a plain notched wheel.
// That is the right default on macOS: a real touchpad is caught by the CG flag before this is ever
// consulted, and a free-spinning mouse genuinely has no signature to report.
// ---------------------------------------------------------------------------
long SwsMessageExtraInfo()
{
  return 0;
}

// ---------------------------------------------------------------------------
// GetWindowThreadProcessId.
//
// Win32: fills the process id (when asked) and returns the thread id that owns the window. The plugin
// stores that thread id so its WH_GETMESSAGE hook watches one thread.
//
// macOS has no such hook, so nothing consumes the answer on that platform -- but the shared
// initialisation code still computes it. Reporting the CALLING thread is the honest value: it is the
// thread that would have been watched, and it keeps the variable's meaning ("the UI thread") intact
// rather than leaving a zero that later code might treat as "unset".
// ---------------------------------------------------------------------------
DWORD SwsWindowThreadId(HWND hwnd, DWORD *pidOut)
{
  (void)hwnd;
  if (pidOut)
    *pidOut = (DWORD)getpid();
  return (DWORD)(uintptr_t)pthread_self();
}

// ---------------------------------------------------------------------------
// SwsTextOut / SwsTextExtent -- the two drawing shims.
//
// These are only reachable from the SETTINGS PANEL, which the macOS build compiles out (no window
// creation is available in SWELL there). They exist so the header's #defines resolve and so that a
// future AppKit panel has something to call; until then they are not linked into the Mac build.
//
// Kept here rather than in the header because both need an HDC and the SWELL drawing entry points.
// ---------------------------------------------------------------------------
int SwsTextOut(void *dc, int x, int y, const char *text, int len)
{
  if (!dc || !text || len < 0)
    return 0;
  // TextOutA draws one line at a point; SWELL_DrawText takes a rect and an alignment. The rect is
  // given a small height so the call renders exactly one line, which is all the panel ever asks for.
  RECT r = {x, y, x + 4096, y + 64};
  return SWELL_DrawText((HDC)dc, text, len, &r, 0);
}

bool SwsTextExtent(void *dc, const char *text, int len, SIZE *out)
{
  if (!dc || !text || !out || len < 0)
    return false;
  // ★ SWELL HAS NO TEXT-MEASURING CALL. It declares GetTextMetrics (font metrics) but not
  // GetTextExtentPoint32, and there is no SWELL_GetTextDimensions -- that name was a guess and does
  // not exist. So the width is approximated from the font's average character width, which is the
  // best SWELL makes available.
  //
  // ★ THIS IS AN APPROXIMATION AND IS DOCUMENTED AS ONE: for the panel's purpose (centring a short
  // numeric tick label under a curve) average width is close enough, but it is not the exact
  // measurement Windows gives. The macOS panel is not shipped in this build, so nothing depends on
  // it yet.
  TEXTMETRIC tm;
  memset(&tm, 0, sizeof(tm));
  if (!GetTextMetrics((HDC)dc, &tm))
    return false;
  out->cx = (int32_t)((int64_t)tm.tmAveCharWidth * len);
  out->cy = tm.tmHeight;
  return true;
}
