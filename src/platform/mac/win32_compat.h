// ---------------------------------------------------------------------------
// platform/mac/win32_compat.h -- the small set of Win32 names SWELL does not provide.
//
// ★ WHY THIS FILE EXISTS. SWELL is Cockos' Win32 emulation layer and ships with the REAPER SDK; it
// is what reaper_plugin.h itself pulls in on non-Windows builds, and it supplies most of what this
// plugin uses (HWND, WM_*, SetTimer, GetDlgItem, SetWindowPos, InvalidateRect, GetWindowLongPtr,
// SetCapture, WindowFromPoint...). It is NOT complete, and the code here uses a handful of names it
// does not define.
//
// ★ STATUS: THIS FILE HAS BEEN COMPILED, NOT RUN. It was written to move the macOS build past the
// type errors measured on 2026-11-03 (206 of them, all from eleven missing names -- see AGENTS 138).
// Compiling is not working, and nothing here has been exercised on a Mac.
//
// ★ THE RULE FOR ADDING TO THIS FILE: only names that are (a) missing from SWELL, (b) used by code
// that is otherwise portable, and (c) expressible exactly. A name whose Win32 semantics cannot be
// reproduced is NOT listed here -- the code that uses it is instead excluded from the macOS build,
// because a wrong definition that compiles is worse than an honest #ifdef. That is why
// RegisterClassExA, the WH_GETMESSAGE hook and the multimedia timer are absent: they are handled by
// platform/mac/*.mm or cut out entirely.
//
// Each entry below says what the Win32 original is and why this definition is faithful.
// ---------------------------------------------------------------------------
#ifndef SWS_MAC_WIN32_COMPAT_H
#define SWS_MAC_WIN32_COMPAT_H

#ifndef _WIN32

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

// ---------------------------------------------------------------------------
// POINTER-TO-CONST-ANYTHING. Win32: `typedef const void *LPCVOID;`. Used only in a function-pointer
// typedef for DwmSetWindowAttribute, which the macOS build does not compile against (dwmapi is a
// Windows library) -- declared here so the typedef parses where it still appears.
// ---------------------------------------------------------------------------
typedef const void *LPCVOID;

// ---------------------------------------------------------------------------
// HANDLE TO A LOADED MODULE. Win32: `HMODULE` is a kernel object handle, usable with LoadLibraryA /
// GetProcAddress / FreeLibrary. Those three are Windows-only, so nothing in the macOS build loads a
// library this way; the type exists only because the same helper signatures mention it.
// ---------------------------------------------------------------------------
typedef void *HMODULE;

// ---------------------------------------------------------------------------
// WIDE-STRING POINTERS. Win32: `LPCWSTR` is `const wchar_t *`. On macOS the plugin does not call any
// W API (SWELL declares the A variants it needs), so these exist for signatures only.
// ---------------------------------------------------------------------------
typedef const wchar_t *LPCWSTR;
typedef wchar_t *LPWSTR;

// ---------------------------------------------------------------------------
// A HOOK HANDLE. Win32: the value returned by SetWindowsHookEx. The macOS build has no such hook --
// the wheel is observed through a CGEventTap (platform/mac/wheel_mac.mm) -- so this is a plain
// opaque pointer used for a null-initialised file-static that is never installed.
// ---------------------------------------------------------------------------
typedef void *HHOOK;

// ---------------------------------------------------------------------------
// SYSTEMTIME. Win32: eight WORD fields, calendar order. Reproduced field for field, because the code
// that formats the log timestamp reads wHour/wMinute/wSecond/wMilliseconds and nothing else. The
// macOS helper that fills it is a small wrapper over localtime() -- see the SystemTimeNow() below --
// so the STRUCT is identical even though the source of the values is not.
// ---------------------------------------------------------------------------
struct SYSTEMTIME
{
  uint16_t wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
};

// ---------------------------------------------------------------------------
// LARGE_INTEGER. Win32: a union of a 64-bit integer and two 32-bit halves. Only `.QuadPart` is used
// here (by the performance-counter fallback), so the union shape is kept for fidelity while only the
// first member is meaningful.
// ---------------------------------------------------------------------------
union LARGE_INTEGER
{
  int64_t QuadPart;
  struct
  {
    uint32_t LowPart;
    int32_t HighPart;
  };
};

// ---------------------------------------------------------------------------
// SIZE. Win32: width and height of a rectangle, as LONGs. Used to receive GetTextExtentPoint32's
// answer (cx, cy). Faithful: two 32-bit signed fields with those names.
// ---------------------------------------------------------------------------
struct SIZE
{
  int32_t cx, cy;
};

// ---------------------------------------------------------------------------
// WNDCLASSA. Win32: the class registration struct, consumed by RegisterClassA. SWELL does not
// declare it because SWELL's own window creation does not go through RegisterClass -- but this
// plugin's panel does, so the STRUCT is here even though RegisterClassA is not (that call is
// Windows-only and the macOS panel is a separate piece of work).
// ---------------------------------------------------------------------------
struct WNDCLASSA
{
  unsigned int style;
  long (*lpfnWndProc)(void *, unsigned int, uintptr_t, intptr_t);
  int cbClsExtra;
  int cbWndExtra;
  void *hInstance;
  void *hIcon;
  void *hCursor;
  void *hbrBackground;
  const char *lpszMenuName;
  const char *lpszClassName;
};

// ---------------------------------------------------------------------------
// TEXTMETRICA. Win32: font metrics. The code reads tmHeight and tmAscent to lay the panel out;
// declared with those two plus the usual prefix fields so offsets stay plausible if anything ever
// reads a third.
// ---------------------------------------------------------------------------
struct TEXTMETRICA
{
  int32_t tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading;
  int32_t tmAveCharWidth, tmMaxCharWidth, tmWeight, tmOverhang;
  int32_t tmDigitizedAspectX, tmDigitizedAspectY;
  char tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
  char tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
};

// ---------------------------------------------------------------------------
// NONCLIENTMETRICS. Win32: the system's window-metric block, requested with
// SystemParametersInfo(SPI_GETNONCLIENTMETRICS). The panel reads lfMessageFont to match the host's
// UI font. Reproduced with the fields the panel touches.
// ---------------------------------------------------------------------------
struct NONCLIENTMETRICS
{
  unsigned int cbSize;
  int32_t iBorderWidth, iScrollWidth, iScrollHeight;
  int32_t iCaptionWidth, iCaptionHeight;
  int32_t iSMCaptionWidth, iSMCaptionHeight;
  int32_t iMenuWidth, iMenuHeight;
  int32_t iPaddedBorderWidth;
  struct
  {
    int32_t lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    char lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision, lfClipPrecision;
    char lfQuality, lfPitchAndFamily;
    char lfFaceName[32];
  } lfMessageFont;
};

// ---------------------------------------------------------------------------
// _snprintf. MinGW's spelling of the bounded formatter; the standard one is snprintf, identical in
// behaviour for the "always NUL terminate" contract this code relies on (every caller checks the
// buffer, and MinGW's _snprintf differs from C99 snprintf only in the truncated-return convention,
// which nothing here uses).
// ---------------------------------------------------------------------------
#define _snprintf snprintf

// ---------------------------------------------------------------------------
// The one helper the compatible types need: fill a SYSTEMTIME from the local clock, matching what
// GetLocalTime does on Windows. Placed here rather than in the logger so that a future Windows build
// keeps using the real GetLocalTime.
// ---------------------------------------------------------------------------
inline void SwsLocalTime(SYSTEMTIME *st)
{
  if (!st)
    return;
  const std::time_t t = std::time(nullptr);
  std::tm lt;
#if defined(_WIN32)
  localtime_s(&lt, &t);
#else
  localtime_r(&t, &lt);
#endif
  st->wYear = (uint16_t)(lt.tm_year + 1900);
  st->wMonth = (uint16_t)(lt.tm_mon + 1);
  st->wDay = (uint16_t)lt.tm_mday;
  st->wDayOfWeek = (uint16_t)lt.tm_wday;
  st->wHour = (uint16_t)lt.tm_hour;
  st->wMinute = (uint16_t)lt.tm_min;
  st->wSecond = (uint16_t)lt.tm_sec;
  st->wMilliseconds = 0; // localtime has no sub-second part; the log's timestamp is second-grained
}

// ===========================================================================
// ★ THE A-SUFFIX MAPPING.
//
// SWELL declares these functions WITHOUT the Win32 ANSI suffix: it has GetClassName, GetWindowText,
// DefWindowProc, SetWindowLong, FindWindowEx, LoadLibrary -- while this codebase (written for
// Windows, where UNICODE is not defined for these calls) spells them GetClassNameA and so on.
//
// Mapping them here with #define is what lets ONE source file serve both platforms without touching
// the ~60 call sites: on Windows the real A functions exist and these defines are not compiled; on
// macOS the names resolve to SWELL's.
//
// ONLY NAME-PAIRS ARE LISTED HERE, one per line, and each must be a function SWELL genuinely
// declares. Anything SWELL does not have at all is either implemented below (group A) or replaced in
// the macOS source (group C).
// ===========================================================================
#define GetClassNameA GetClassName
#define SendMessageA SendMessage
#define PostMessageA PostMessage
#define GetWindowTextA GetWindowText
#define SetWindowTextA SetWindowText
#define DefWindowProcA DefWindowProc
#define FindWindowExA FindWindowEx
#define LoadLibraryA LoadLibrary
#define GetTextMetricsA GetTextMetrics
#define GetObjectA GetObject
#define CreateFontIndirectA CreateFontIndirect
#define DeleteObject_ DeleteObject
#define GetWindowLongA GetWindowLong
#define SetWindowLongA SetWindowLong
// SWELL's long-pointer accessors are the same functions as its plain ones (a pointer-sized LONG_PTR
// is what it returns anyway), so the Ptr spelling maps straight across.
#define GetWindowLongPtrA GetWindowLong
#define SetWindowLongPtrA SetWindowLong
#define GetWindowLongPtrW GetWindowLong
#define SetWindowLongPtrW SetWindowLong
// SWELL's DrawText takes the alignment in the same parameter position; the signature matches.
#define DrawTextA SWELL_DrawText
// No TextOut in SWELL: drawing a single line of text is SWELL_DrawText with a one-line rect, and the
// code below only ever uses it that way (see SwsTextOut).
#define TextOutA SwsTextOut
// No GetTextExtentPoint32: SWELL measures through SWELL_GetTextDimensions (see SwsTextExtent).
#define GetTextExtentPoint32A SwsTextExtent

// ---------------------------------------------------------------------------
// GROUP A2: CONSTANTS AND SMALL VALUES SWELL DOES NOT DEFINE.
//
// These are not implementations and carry no risk of semantic drift -- each is a number Windows
// fixed long ago and nothing on macOS can change. They are listed with their Windows value so a
// reader can check one against the Win32 header.
// ---------------------------------------------------------------------------

// The wheel's full-notch delta. Windows has been 120 since scroll wheels existed, and the plugin's
// own model is calibrated in these units (kWheelUnit == 120 in platform.h).
#ifndef WHEEL_DELTA
#define WHEEL_DELTA 120
#endif

// The first id an application may use for its own messages. Both platforms reserve WM_APP; SWELL
// defines it, and this is only a guard in case an older SWELL does not.
#ifndef WM_APP
#define WM_APP 0x8000
#endif

// A message-only window: CREATESTRUCT puts it in the message-only list rather than on screen. SWELL
// does not define it; the animation tick window on macOS does not need to be a real window anyway
// (see platform/mac/time_mac.mm, which uses a dispatch source instead).
#ifndef HWND_MESSAGE
#define HWND_MESSAGE ((HWND)(-3))
#endif

// RegisterClass failing with this error means the class is already registered, which is success for
// the caller's purpose. Windows' value is 1410.
#ifndef ERROR_CLASS_ALREADY_EXISTS
#define ERROR_CLASS_ALREADY_EXISTS 1410L
#endif

// timeSetEvent flags. Only used by the multimedia-timer path, which the macOS build does not take;
// defined so the dormant code still parses.
#ifndef TIME_ONESHOT
#define TIME_ONESHOT 0x0000
#endif
#ifndef TIME_PERIODIC
#define TIME_PERIODIC 0x0001
#endif

// Hook and message-pump constants referenced by the Windows hook procedure.
#ifndef HC_ACTION
#define HC_ACTION 0
#endif
#ifndef PM_REMOVE
#define PM_REMOVE 0x0001
#endif
#ifndef WH_GETMESSAGE
#define WH_GETMESSAGE 3
#endif

// WM_NULL: "no message". The Windows hook sets msg->message to this to swallow a wheel, so the
// surface does not also move natively. On macOS the same effect is achieved by the event tap not
// being an observer-only tap for those events (see platform/mac/wheel_mac.mm) -- but the constant is
// still referenced by the shared delivery code, which is compiled on both platforms.
#ifndef WM_NULL
#define WM_NULL 0x0000
#endif

// The multimedia-timer success code. Only used by the Windows path; defined so the dormant timer
// code parses on macOS.
#ifndef TIMERR_NOERROR
#define TIMERR_NOERROR 0
#endif

// GetWindowThreadProcessId. Win32: the thread and process that own a window. This plugin uses it to
// remember which thread its message hook should watch. macOS has no per-window thread id and does
// not install a Win32 hook at all, so this reports the calling thread's id, which is the same value
// the hook would need if one existed.
DWORD SwsWindowThreadId(HWND hwnd, DWORD *pidOut);
#define GetWindowThreadProcessId(hwnd, pid) SwsWindowThreadId((hwnd), (pid))

// GetMessageExtraInfo. Win32: an extra 32-bit value the input system attaches to the most recent
// mouse message. This plugin reads it to identify the device (a touchpad's signature lives there).
// macOS has no such side channel -- the touchpad is identified from the event itself, see
// platform/mac/wheel_mac.mm -- so it reports 0, which the device classifier already treats as
// "no signature", i.e. a plain wheel.
long SwsMessageExtraInfo();
#define GetMessageExtraInfo() SwsMessageExtraInfo()

// ---------------------------------------------------------------------------
// GROUP A: names SWELL does not have, implemented here from the platform's own primitives.
// Each is documented with what the Win32 original guarantees and why this matches.
// ---------------------------------------------------------------------------

// InterlockedExchange / InterlockedCompareExchange. Win32: atomic 32-bit read-modify-write with full
// barriers. The plugin uses them to coalesce timer ticks and to publish the "last device" record
// between threads. Implemented with GCC/Clang builtins, which give the same atomicity and ordering.
//
// ★ THE VALUE TYPE IS SWELL's LONG, NOT C's long. On Windows LONG is always 32-bit; here `long` is
// 64-bit on Apple Silicon and would make every call site a type error. SWELL defines LONG itself, so
// that is the type used -- which also keeps the signatures identical to the Windows build.
inline LONG SwsInterlockedExchange(volatile LONG *target, LONG value)
{
  return __sync_lock_test_and_set(target, value);
}
inline LONG SwsInterlockedCompareExchange(volatile LONG *target, LONG exchange, LONG comparand)
{
  return __sync_val_compare_and_swap(target, comparand, exchange);
}
#define InterlockedExchange(target, value) SwsInterlockedExchange((target), (value))
#define InterlockedCompareExchange(target, exchange, comparand) \
  SwsInterlockedCompareExchange((target), (exchange), (comparand))
#define InterlockedIncrement(target) ((LONG)__sync_add_and_fetch((target), 1))
#define InterlockedDecrement(target) ((LONG)__sync_sub_and_fetch((target), 1))

// GetTickCount / GetTickCount64. Win32: milliseconds since boot, wrapping after 49.7 days. macOS has
// no such counter; the monotonic clock from platform.h is the same quantity and does not wrap in any
// span this plugin cares about. Declared as functions so the two spellings cannot drift.
LONG SwsTickCount();
unsigned long long SwsTickCount64();
#define GetTickCount() SwsTickCount()
#define GetTickCount64() SwsTickCount64()

// GetKeyState. Win32: the asynchronous key state, high bit set when the key is down. Used once, to
// ask whether either Win/Super key is held when reading a mouse-modifier assignment. On macOS the
// equivalent is CGEventSourceKeyState over the Command key -- but that needs ApplicationServices,
// which this file deliberately does not include; SwsKeyDown is implemented next to the event tap
// (platform/mac/wheel_mac.mm) for exactly that reason.
bool SwsKeyDown(int vk);
#define GetKeyState(vk) (SwsKeyDown(vk) ? (short)0x8000 : (short)0)

// GetTempPathA. Win32: the directory for temporary files, with a trailing separator. The plugin's
// debug log lives there. On macOS that is $TMPDIR (falling back to /tmp). Returns the length, as the
// Win32 call does, and 0 on failure.
unsigned int SwsTempPath(char *out, unsigned int outSize);
#define GetTempPathA(n, buf) SwsTempPath((buf), (n))

// GetDlgCtrlID. Win32: the control id of a child window (the HMENU it was created with). SWELL has
// no equivalent accessor, so the id is carried in the window's own user data is NOT done here --
// instead the panel looks its controls up by id, which is the direction it already uses everywhere.
// The one place that needs the reverse is the fader/knob hot path; see the macOS note in
// smooth_wheel_scroll.cpp for how that call site is handled.
int SwsCtrlIdOf(void *hwnd);
#define GetDlgCtrlID(hwnd) SwsCtrlIdOf((hwnd))

// GetScrollInfo. Win32: reads a scrollbar's range/page/position. The panel uses it to decide whether
// the content is taller than the window.
//
// ★ SCROLLINFO IS SWELL'S, NOT OURS. SWELL already declares the struct (swell-types.h), so this file
// must not define it again -- that would be a redefinition error rather than a missing type. Only
// the CALL is absent from SWELL, so only the call is provided.
bool SwsGetScrollInfo(void *hwnd, int bar, SCROLLINFO *si);
#define GetScrollInfo(hwnd, bar, si) SwsGetScrollInfo((hwnd), (bar), (si))

// QueryPerformanceCounter / QueryPerformanceFrequency. Win32: the high-resolution timer, in ticks
// whose rate the frequency call reports. The plugin uses the pair for its monotonic seconds clock.
//
// Implemented over plat::Now(), which is ALREADY in milliseconds from mach_absolute_time -- so the
// frequency is exactly 1000 and a counter tick is a millisecond. That is a coarser unit than Windows'
// 100 ns counter, and it is enough: the value is only ever used as a duration in seconds, and the
// model works in milliseconds throughout.
BOOL SwsQueryPerformanceFrequency(LARGE_INTEGER *f);
BOOL SwsQueryPerformanceCounter(LARGE_INTEGER *c);
#define QueryPerformanceFrequency(f) SwsQueryPerformanceFrequency((f))
#define QueryPerformanceCounter(c) SwsQueryPerformanceCounter((c))

// LoadLibrary / GetProcAddress / FreeLibrary exist in SWELL for loading plugins, but the two calls
// in this file load dwmapi.dll and uxtheme.dll -- Windows-only libraries that macOS does not have.
// The whole helpers are excluded from the macOS build instead of being faked, so these are not
// mapped; see the #ifdef around ApplyTitleBar / ApplyScrollbarTheme.
unsigned long SwsLastError();
#define GetLastError() SwsLastError()

// Two drawing shims, because SWELL's text API differs in shape rather than just in name.
//
//   SwsTextOut     : TextOutA(dc, x, y, text, len) -- SWELL_DrawText takes a RECT, so the rect is
//                    built here. Alignment 0 = left/top, matching TextOut's default.
//   SwsTextExtent  : GetTextExtentPoint32A fills a SIZE with the measured width/height.
// Both need an HDC, so their definitions live where the panel does (smooth_wheel_scroll.cpp) and are
// declared here for the same reason the others are.
int SwsTextOut(void *dc, int x, int y, const char *text, int len);
bool SwsTextExtent(void *dc, const char *text, int len, SIZE *out);

#endif // !_WIN32

#endif // SWS_MAC_WIN32_COMPAT_H
