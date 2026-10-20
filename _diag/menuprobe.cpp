// MENU WINDOW PROBE -- what exactly IS a REAPER popup menu, and can its background be touched?
//
//   g++ -std=c++17 -O2 -o menuprobe.exe menuprobe.cpp -luser32 -lgdi32 && ./menuprobe.exe
//
// WHY: a request to give REAPER's popup menus an acrylic background. Two mature projects that do this
// for Windows menus (AcrylicMenus, TranslucentFlyouts) both explicitly EXCLUDE "ownerdrawn popup
// menus", because those paint their own content and cannot be restyled from outside. So the whole
// question reduces to measurable facts about the menu window:
//
//   1. Is it the standard menu class (#32768) or a custom window?  A standard one has the normal
//      non-client frame and a WM_ERASEBKGND we could intercept.
//   2. Are its ITEMS owner-drawn (MF_OWNERDRAW)?  If they are, the background comes from REAPER's own
//      painting and no window-level effect can show through.
//   3. What window styles does it carry?  WS_EX_LAYERED would let a blur be applied directly.
//   4. Can a subclass see its background messages at all?
//
// The probe is READ-ONLY except for the temporary subclass in step 4, which it installs and removes
// immediately. It changes nothing about REAPER and writes nothing.

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
static void Check(bool ok, const char *what)
{
  printf("  %-70s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    ++g_fail;
}

// Does any window of this process use the menu class?
static HWND g_menu = nullptr;
static DWORD g_reaperPid = 0;

static BOOL CALLBACK FindMenuProc(HWND h, LPARAM)
{
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (pid != g_reaperPid)
    return TRUE;
  char cls[64] = {0};
  GetClassNameA(h, cls, sizeof(cls));
  if (strcmp(cls, "#32768") == 0)
  {
    g_menu = h;
    return FALSE;
  }
  return TRUE;
}

static HWND FindMenuWindow(DWORD pid)
{
  g_reaperPid = pid;
  g_menu = nullptr;
  for (HWND top = GetTopWindow(nullptr); top; top = GetWindow(top, GW_HWNDNEXT))
  {
    DWORD tpid = 0;
    GetWindowThreadProcessId(top, &tpid);
    if (tpid != pid)
      continue;
    FindMenuProc(top, 0);
    if (g_menu)
      return g_menu;
    EnumChildWindows(top, FindMenuProc, 0);
    if (g_menu)
      return g_menu;
  }
  return nullptr;
}

// --- Step 4's instrumentation: count the messages a menu window receives. ---
static volatile LONG g_eraseCount = 0;
static volatile LONG g_paintCount = 0;
static volatile LONG g_ncPaintCount = 0;
static volatile LONG g_printCount = 0;

static LRESULT CALLBACK MenuSubclassProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
  switch (msg)
  {
  case WM_ERASEBKGND: InterlockedIncrement(&g_eraseCount); break;
  case WM_PAINT: InterlockedIncrement(&g_paintCount); break;
  case WM_NCPAINT: InterlockedIncrement(&g_ncPaintCount); break;
  case WM_PRINTCLIENT: InterlockedIncrement(&g_printCount); break;
  }
  return DefSubclassProc(h, msg, wp, lp);
}

int main()
{
  printf("REAPER popup menu probe (read-only)\n\n");

  HWND main = FindWindowA("REAPERwnd", nullptr);
  if (!main)
  {
    printf("REAPERwnd not found -- is REAPER running?\n");
    return 1;
  }
  DWORD pid = 0;
  GetWindowThreadProcessId(main, &pid);
  printf("REAPER pid = %lu\n\n", (unsigned long)pid);

  // THE MENU IS A TRANSIENT WINDOW. It exists only while it is being tracked, so a probe that looks
  // once and reports is at the mercy of timing: it can catch a menu that is already being torn down
  // and then "fail" for reasons that say nothing about the menu. Measured that way once already --
  // GetMenu returned null and SetWindowSubclass failed with err=0 (the signature of a dead HWND).
  //
  // So this WATCHES instead: it samples repeatedly for a few seconds and keeps the FIRST menu window
  // it finds while that window is still valid, which is what a live menu looks like.
  printf("  looking for a menu window (watching for up to 20 s -- open a REAPER context menu now)\n");
  HWND menu = nullptr;
  for (int i = 0; i < 200 && !menu; ++i)
  {
    HWND cand = FindMenuWindow(pid);
    if (cand && IsWindow(cand) && IsWindowVisible(cand))
    {
      RECT cr = {0};
      GetWindowRect(cand, &cr);
      // ONLY "exists, visible, and has a real size" -- nothing else.
      //
      // The previous version also required GetMenu(cand) to be non-null, on the assumption that a
      // live menu must be attached to its menu object. That assumption is WRONG for a window the
      // application created itself for TrackPopupMenu, and it filtered out the very window we were
      // looking for: the probe reported "no live menu found" in 20 s while a visible #32768 of the
      // right size was demonstrably there. Never gate a search on a property that has not itself
      // been verified -- it turns a wrong guess into a silent empty result.
      if ((cr.right - cr.left) > 20 && (cr.bottom - cr.top) > 20)
      {
        menu = cand;
        printf("      found a visible menu after %d ms: hwnd=%p\n", i * 100, (void *)menu);
        break;
      }
    }
    Sleep(100);
  }
  if (!menu)
  {
    printf("\n  NO VISIBLE MENU FOUND in 20 s.\n");
    printf("  Right-click in the arrange area and LEAVE THE MENU OPEN while this runs.\n");
    return 2;
  }

  // --- 1. What is it? ---
  printf("  [1] the menu window\n");
  char cls[64] = {0};
  GetClassNameA(menu, cls, sizeof(cls));
  RECT r = {0};
  GetWindowRect(menu, &r);
  printf("      hwnd=%p  class=%s  rect=%ld,%ld,%ld,%ld (%ldx%ld)\n", (void *)menu, cls, r.left,
         r.top, r.right, r.bottom, r.right - r.left, r.bottom - r.top);
  Check(strcmp(cls, "#32768") == 0,
        "it is the STANDARD menu class #32768 (not a custom window)");

  // --- 2. Styles: is it layered already, and how is it drawn? ---
  printf("\n  [2] window styles\n");
  const LONG_PTR style = GetWindowLongPtrA(menu, GWL_STYLE);
  const LONG_PTR ex = GetWindowLongPtrA(menu, GWL_EXSTYLE);
  printf("      style     = 0x%08lX\n", (unsigned long)style);
  printf("      exstyle   = 0x%08lX\n", (unsigned long)ex);
  printf("      WS_EX_LAYERED      : %s\n", (ex & WS_EX_LAYERED) ? "yes" : "no");
  printf("      WS_EX_TRANSPARENT  : %s\n", (ex & WS_EX_TRANSPARENT) ? "yes" : "no");
  printf("      WS_EX_COMPOSITED   : %s\n", (ex & WS_EX_COMPOSITED) ? "yes" : "no");
  printf("      WS_BORDER          : %s\n", (style & WS_BORDER) ? "yes" : "no");
  printf("      WS_POPUP           : %s\n", (style & WS_POPUP) ? "yes" : "no");
  printf("      (WS_EX_LAYERED would allow a blur to be applied directly; without it, the window\n");
  printf("       has to be made layered first, which changes how it is composited)\n");

  // --- 3. THE DECIDING FACT: are the items owner-drawn? ---
  //
  // A standard menu's items are drawn by the system from MENUITEMINFO, and the background behind them
  // is the menu's own background -- interceptable. An MF_OWNERDRAW item means the application paints
  // that item itself, so the pixels come from REAPER and no outside effect can show through them.
  //
  // NOTE: a menu HANDLE is not a window handle. The window is #32768; the menu itself is a separate
  // object, retrieved from that window with GetMenu. Passing the HWND to the menu calls does not
  // compile, and passing it anyway would be the kind of mistake that silently reads nothing.
  printf("\n  [3] the deciding fact: are the menu ITEMS owner-drawn?\n");
  {
    HMENU hmenu = GetMenu(menu);
    printf("      GetMenu(hwnd) = %p\n", (void *)hmenu);
    if (!hmenu)
    {
      printf("      >>> NO MENU ATTACHED to the #32768 window.\n"
             "          A TrackPopupMenu menu is normally attached only while it is being tracked.\n"
             "          Leave the menu OPEN and run this again.\n");
    }
    else
    {
      const int count = GetMenuItemCount(hmenu);
      printf("      item count = %d\n", count);
      int ownerdraw = 0, checked = 0;
      for (int i = 0; i < count && i < 200; ++i)
      {
        MENUITEMINFOA one;
        memset(&one, 0, sizeof(one));
        one.cbSize = sizeof(one);
        one.fMask = MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU;
        if (!GetMenuItemInfoA(hmenu, (UINT)i, TRUE, &one))
          continue;
        ++checked;
        if (one.fType & MFT_OWNERDRAW)
          ++ownerdraw;
      }
      printf("      items read = %d, of which MFT_OWNERDRAW = %d\n", checked, ownerdraw);
      if (checked == 0)
        printf("      >>> could not read any item -- inconclusive, do NOT draw a conclusion\n");
      else if (ownerdraw == 0)
        printf("      >>> NOT owner-drawn: the system draws these items, so the background behind\n"
               "          them is the menu's own -- an outside effect has something to show through\n");
      else if (ownerdraw == checked)
        printf("      >>> ALL owner-drawn: REAPER paints every item itself. A window-level acrylic\n"
               "          can still blur the BACKGROUND, but each item will paint its own opaque\n"
               "          pixels on top, so the look will be patchy unless REAPER's colours are\n"
               "          themselves made translucent (which means touching ITS painting)\n");
      else
        printf("      >>> MIXED: some items are owner-drawn and some are not (often the separators,\n"
               "          which are system-drawn)\n");
    }
    Check(true, "recorded the owner-draw fact (this is the one that decides feasibility)");
  }

  // --- 4. Can a subclass see the drawing messages at all? ---
  //
  // If WM_ERASEBKGND / WM_PAINT reach a subclass, then a variant of the mixer's approach exists: let
  // the menu draw, but intercept the background. Counted over a short window while the menu stays up.
  printf("\n  [4] do the drawing messages reach a subclass? (installed briefly, then removed)\n");
  {
    if (!IsWindow(menu))
    {
      printf("      the menu window is GONE -- it was dismissed before this step.\n");
      printf("      INCONCLUSIVE: no conclusion may be drawn about subclassing.\n");
    }
    else
    {
      SetLastError(0);
      const BOOL ok = SetWindowSubclass(menu, MenuSubclassProc, 1, 0);
      const DWORD err = GetLastError();
      printf("      SetWindowSubclass: %s (err=%lu)\n", ok ? "ok" : "FAILED", (unsigned long)err);
      if (!ok)
      {
        // err==0 with a failure usually means the HWND was not valid; a real cross-process refusal
        // reports ERROR_ACCESS_DENIED (5). Say which, instead of just "FAILED".
        printf("      err=0 means no error was set, which is what an INVALID window handle looks\n");
        printf("      like -- so treat this as INCONCLUSIVE, not as 'subclassing is impossible'.\n");
      }
      else
      {
        InvalidateRect(menu, nullptr, TRUE);
        UpdateWindow(menu);
        RedrawWindow(menu, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_FRAME);
        Sleep(600);
        printf("      WM_ERASEBKGND=%ld  WM_PAINT=%ld  WM_NCPAINT=%ld  WM_PRINTCLIENT=%ld\n",
               (long)g_eraseCount, (long)g_paintCount, (long)g_ncPaintCount, (long)g_printCount);
        const bool sawDraw = (g_eraseCount > 0 || g_paintCount > 0 || g_ncPaintCount > 0);
        Check(sawDraw, "a subclass DOES receive the menu's drawing messages");
        RemoveWindowSubclass(menu, MenuSubclassProc, 1);
        printf("      (subclass removed again; the menu is untouched)\n");
      }
    }
  }

  printf("\n  --- how to read this ---\n");
  printf("  * [1] #32768            -> the window is standard, so window-level effects are applicable.\n");
  printf("  * [3] owner-draw = 0    -> the SYSTEM draws the items: the cleanest case.\n");
  printf("        all owner-drawn   -> REAPER draws them; only its own colours can be translucent.\n");
  printf("  * [4] messages arrive   -> intercepting the background is possible in principle.\n");
  printf("  None of this says the result LOOKS right; that is a separate question and needs a\n");
  printf("  prototype on screen.\n");

  printf("\n%s\n", g_fail ? "FAIL" : "done: read [1]-[4] above");
  return g_fail ? 1 : 0;
}
