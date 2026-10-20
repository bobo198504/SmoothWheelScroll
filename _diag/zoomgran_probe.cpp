// ZOOM GRANULARITY PROBE -- what is the SMALLEST relative value action 1000 accepts?
//
//   build:  g++ -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ \
//                -DWIN32 -D_WIN32 -DNOMINMAX -DUNICODE -D_UNICODE \
//                -I../third_party/reaper-sdk-git/sdk zoomgran_probe.cpp \
//                -o ../build/reaper_zoomgran_probe.dll -luser32 -lgdi32
//   copy  build/reaper_zoomgran_probe.dll  into  D:\REAPER\UserPlugins  and restart REAPER.
//
// WHY THIS EXISTS
//
// The plugin delivers main-view vertical ZOOM (1000/1001) in WHOLE 7-bit units
// (Delivery::kStepUnits, routing.h). One notch of wheel is 2.28 units of travel in the 1.6.1 curve
// model, so the receiver gets 2 sends of `val=127` -- two whole steps -- and the gesture reads as
// "the plugin is not doing anything, it just steps" (user report, 2026-10-13).
//
// The user's proposal is to STOP special-casing this axis and feed it a fine stream like a touchpad.
// That was tried once (AGENTS 109) and reverted because the axis "jittered" (AGENTS 110), with the
// standing explanation "this receiver cannot act on a sub-unit step". That explanation was never
// MEASURED -- 109/110 recorded the symptom, not the threshold. This probe measures the threshold.
//
// METHOD
//
//   For each size in a fixed ladder from 1/3840 unit up to 4 units:
//     read track 0's TCP height, send that size N times, read the height again.
//   The total change over N sends is the answer. A size that moves 0 px is NOT ACCEPTED.
//
// Running it needs no key binding: it registers its own actions, so it is started from REAPER's
// Actions window. It NEVER sends anything on its own -- only when one of its actions is invoked.
//
// The run writes  <reaper resource path>/zoomgran_probe.txt  and echoes to the REAPER console.

#include <cstdio>
#include <cstring>
#include <cmath>

// The SDK's functions are not linked: they are resolved at load time out of the host. The macro
// below makes this translation unit DEFINE them, and REAPERAPI_LoadAPI fills them in from
// rec->GetFunc. Without both, every SDK call is an undefined reference at link time.
//
// NOT `REAPERAPI_MINIMAL`: with MINIMAL, REAPERAPI_LoadAPI resolves ONLY the functions this file
// names, and returns non-zero if one of them is absent -- which silently killed the whole DLL
// (the entry point returned 0 and nothing was registered). The main plugin uses the full API, and
// so does this probe now: it costs nothing at runtime (the loader walks the same table either way)
// and it cannot fail on a function the probe does not actually need.
#define REAPERAPI_IMPLEMENT

#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"

static reaper_plugin_info_t *g_rec = nullptr;

// Defined at the bottom (it is the entry point's own instrumentation), declared here so the action
// handlers can mark themselves too.
static void WriteLoadMarker(const char *what);

// ---------------------------------------------------------------------------------------------
// View state
// ---------------------------------------------------------------------------------------------
//
// The arrange view's vertical zoom is expressed as a TRACK HEIGHT, so the natural reading is track
// 0's TCP height ("I_TCPH") -- the same reading the row-height compensation uses. It is a PIXEL
// height, hence quantized, and that is exactly the question: how many pixels does a sub-unit send
// move it.

static double ReadTrackHeight()
{
  MediaTrack *tr = GetTrack((ReaProject *)nullptr, 0);  if (!tr)
    return -1.0;
  return GetMediaTrackInfo_Value(tr, "I_TCPH");
}

static void ReportState(const char *tag)
{
  char buf[320];
  _snprintf(buf, sizeof(buf), "[zoomgran] %-12s track0 I_TCPH = %.1f   tracks = %d\n", tag,
            ReadTrackHeight(), CountTracks(nullptr));  ShowConsoleMsg(buf);
}

// ---------------------------------------------------------------------------------------------
// Sending a relative value
// ---------------------------------------------------------------------------------------------
//
// SENDING A RELATIVE VALUE -- AND THE FIRST VERSION OF THIS PROBE GOT IT WRONG.
//
// The ladder measured 0 px for EVERY rung, including 4 units x 24 (= 96 whole notches), which
// cannot be a receiver refusing sub-unit steps -- it meant the sends were malformed.
//
// The encoding is REAPER's own, and it is NOT "truncate to a whole number and put the fraction in
// valhw". Copied from the plugin's EncodeRel1 (src/smooth_wheel_scroll.cpp:1607), which is itself
// copied from the SDK's cSurf::encode_relmode1_extended ("copied from kbd.cpp"):
//
//   positive d:  val7bit = ceil(d)          <-- ROUNDS UP, away from zero
//                z       = (val7bit - d) * 256      <-- z is the SHORTFALL, not the fraction
//   negative d:  val7bit = floor(d)         <-- ROUNDS DOWN, away from zero
//                z       = (d - val7bit) * 256
//   then:        val   = val7bit & 0x7f     <-- SEVEN-BIT two's complement
//                valhw = -1 - z
//
// Two things were wrong before: `val` was sent as an ordinary signed int (REAPER reads the low 7
// bits, so e.g. -1 went out as 0x7f = +127, the opposite direction), and `z` carried the fraction
// instead of the shortfall.
static void SendRelative(int command, double units)
{
  if (units == 0.0)
    return;

  int val7bit = 0, z = 0;
  if (units < 0.0)
  {
    const double w = floor(units);
    if (w >= -64.0)
    {
      val7bit = (int)w;
      z = (int)((units - w) * 256.0 + 0.5);
    }
    else
      val7bit = -64;
  }
  else
  {
    const double w = ceil(units);
    if (w <= 63.0)
    {
      val7bit = (int)w;
      z = (int)((w - units) * 256.0 + 0.5);
    }
    else
      val7bit = 63;
  }
  const int val = val7bit & 0x7f;
  const int valhw = -1 - (z > 255 ? 255 : z);
  KBD_OnMainActionEx(command, val, valhw, 1, nullptr, nullptr);
}

// ---------------------------------------------------------------------------------------------
// The measurement
// ---------------------------------------------------------------------------------------------

static const int kSendsPerRung = 24; // enough to accumulate, small enough not to run away

static double g_movedTotal = 0.0;

static void MeasureRung(FILE *out, int command, double units, const char *label)
{
  const double before = ReadTrackHeight();
  for (int i = 0; i < kSendsPerRung; ++i)
    SendRelative(command, units);
  const double after = ReadTrackHeight();
  const double moved = after - before;
  g_movedTotal += moved;

  char buf[400];
  _snprintf(buf, sizeof(buf),
            "  %-14s %11.7f u x%3d   height %7.1f -> %7.1f   moved %+7.1f px (%+.5f px/send)%s\n",
            label, units, kSendsPerRung, before, after, moved, moved / (double)kSendsPerRung,
            (moved == 0.0) ? "   <== NOT ACCEPTED" : "");
  if (out)
    fputs(buf, out);
  ShowConsoleMsg(buf);
  if (out)
    fflush(out);
}

struct Rung
{
  const char *label;
  double units;
};

// Powers of two, so "the smallest accepted increment" comes out AS one of these rather than as an
// interpolated guess.
static const Rung kLadder[] = {
    {"1/3840 unit", 1.0 / 3840.0},
    {"1/1024 unit", 1.0 / 1024.0},
    {"1/256 unit", 1.0 / 256.0},
    {"1/64 unit", 1.0 / 64.0},
    {"1/16 unit", 1.0 / 16.0},
    {"1/8 unit", 1.0 / 8.0},
    {"1/4 unit", 1.0 / 4.0},
    {"1/2 unit", 1.0 / 2.0},
    {"3/4 unit", 3.0 / 4.0},
    {"1 unit", 1.0},
    {"2 units", 2.0},
    {"4 units", 4.0},
};
static const int kLadderN = (int)(sizeof(kLadder) / sizeof(kLadder[0]));

// A BLUNT SANITY CHECK, run first. If a LARGE send does not move the view AT ALL, then the probe's
// method is broken (the send, the command, or the reading) and every rung below it measures
// nothing for that reason -- which is exactly what happened: the whole ladder read 0 px, including
// 4 units x 24 (= 96 whole notches), and 96 notches cannot be refused by any receiver.
//
// So this sends ONE big value and reports the reading before and after. Two outcomes:
//   * the view visibly zooms and the height reading still says 0  -> the READING is wrong (I_TCPH
//     does not follow the vertical zoom), and the ladder must be judged by a different number
//   * the view does not zoom                                      -> the SEND is wrong
static void SanityCheck(int command, FILE *out)
{
  const double before = ReadTrackHeight();
  for (int i = 0; i < 8; ++i)
    SendRelative(command, 20.0); // 20 units, eight times: unmistakable if it lands
  const double after = ReadTrackHeight();

  char buf[400];
  _snprintf(buf, sizeof(buf),
            "SANITY: 20 units x 8 sends   height %.1f -> %.1f   (moved %+.1f)\n"
            "        IF THE VIEW DID NOT VISIBLY ZOOM, the send is broken -- stop and fix that\n"
            "        before reading any rung below. IF IT DID ZOOM but this says 0, then I_TCPH\n"
            "        is not the reading to use and the ladder numbers are meaningless.\n\n",
            before, after, after - before);
  if (out)
    fputs(buf, out);
  ShowConsoleMsg(buf);
  if (out)
    fflush(out);
  g_movedTotal += (after - before);
}

static void RunLadder(int command, const char *what)
{
  // WHERE THE OUTPUT GOES. GetResourcePath() is the correct answer, but it returns a pointer into
  // REAPER and cannot be trusted to be non-null here (a null would have been dereferenced by the
  // snprintf below, which is exactly the kind of silent failure this probe exists to avoid). So the
  // known resource paths are tried first, in order, and the first that OPENS is the one used.
  const char *cands[] = {
      "D:\\REAPER\\zoomgran_probe.txt",
      "C:\\Users\\EBo\\AppData\\Roaming\\REAPER\\zoomgran_probe.txt",
      "zoomgran_probe.txt",
  };
  FILE *out = nullptr;
  for (int i = 0; i < 3 && !out; ++i)
    out = fopen(cands[i], "wb");
  WriteLoadMarker("RunLadder: file opened");

  char head[640];
  _snprintf(head, sizeof(head),
            "zoom granularity probe\n"
            "axis   : %s (command %d)\n"
            "method : send the size %d times, report how far track 0's TCP height moved\n"
            "reading: a size that moves 0 px is NOT ACCEPTED by the receiver\n\n",
            what, command, kSendsPerRung);
  if (out)
    fputs(head, out);
  ShowConsoleMsg(head);
  WriteLoadMarker("RunLadder: header written");

  ReportState("before");
  const double startH = ReadTrackHeight();
  WriteLoadMarker("RunLadder: state read, starting the ladder");

  // ⚠ WATCH THE VIEW WHILE THIS RUNS. The sanity check sends 160 units; the arrange view must
  // visibly zoom during it. If it does not, nothing below means anything.
  SanityCheck(command, out);
  WriteLoadMarker("RunLadder: sanity check done");

  // ASCENDING, so the small rungs are measured before the view has been pushed far.
  for (int i = 0; i < kLadderN; ++i)
  {
    char m[64];
    _snprintf(m, sizeof(m), "  rung %d/%d %s", i + 1, kLadderN, kLadder[i].label);
    WriteLoadMarker(m);
    MeasureRung(out, command, kLadder[i].units, kLadder[i].label);
  }
  WriteLoadMarker("RunLadder: ladder complete");

  ReportState("after");
  const double endH = ReadTrackHeight();

  char tail[640];
  _snprintf(tail, sizeof(tail),
            "\nheight %.1f -> %.1f over the whole ladder.\n"
            "IF THE VIEW IS AT THE MINIMUM OR MAXIMUM ZOOM the small rungs read 0 for that\n"
            "reason instead of because they are rejected. Re-run with the view zoomed to\n"
            "somewhere in the middle to tell those two apart.\n"
            "'undo the movement' sends one opposite value to bring it back.\n",
            startH, endH);
  if (out)
    fputs(tail, out);
  ShowConsoleMsg(tail);
  if (out)
    fclose(out);
}

// ---------------------------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------------------------

static int g_cmdRun = 0, g_cmdReset = 0, g_cmdState = 0;
static bool g_registered = false;

static void DoRun()
{
  WriteLoadMarker("ACTION: run the ladder invoked");
  RunLadder(1000, "View: Zoom vertically");
}

static void DoReset()
{
  if (g_movedTotal != 0.0)
  {
    SendRelative(1000, -g_movedTotal);
    g_movedTotal = 0.0;
  }
  ReportState("after reset");
}

static void DoState() { ReportState("state"); }

static void RegisterActions()
{
  if (g_registered || !g_rec)
    return;
  custom_action_register_t a;
  memset(&a, 0, sizeof(a));
  a.uniqueSectionId = 0;

  a.idStr = "ZOOMGRAN_RUN";
  a.name = "Zoom granularity probe: run the ladder";
  g_cmdRun = g_rec->Register("custom_action", &a);

  a.idStr = "ZOOMGRAN_RESET";
  a.name = "Zoom granularity probe: undo the movement";
  g_cmdReset = g_rec->Register("custom_action", &a);

  a.idStr = "ZOOMGRAN_STATE";
  a.name = "Zoom granularity probe: print the current state";
  g_cmdState = g_rec->Register("custom_action", &a);

  g_registered = true;
}

// hookcommand2 signature: bool (*)(KbdSectionInfo*, int command, int val, int valhw, int relmode,
// HWND hwnd). Returning true means "handled".
static bool HookCommand2(KbdSectionInfo *sec, int command, int val, int valhw, int relmode,
                         HWND hwnd)
{
  (void)sec; (void)val; (void)valhw; (void)relmode; (void)hwnd;
  if (command == g_cmdRun)
  {
    DoRun();
    return true;
  }
  if (command == g_cmdReset)
  {
    DoReset();
    return true;
  }
  if (command == g_cmdState)
  {
    DoState();
    return true;
  }
  return false;
}

// The entry point. The SDK's macro expands to the NAME, but the SIGNATURE is
//   int ReaperPluginEntry(HINSTANCE hInst, reaper_plugin_info_t *rec)
// -- two arguments. Declaring only `rec` produced a DLL whose export table was empty, so REAPER
// never loaded it and none of its actions appeared in the Actions window.
// A LOAD-TIME MARKER. Written the moment REAPER calls the entry point, before anything else, so a
// missing output file can be told apart from a missing DLL: if this file appears but the ladder's
// does not, the DLL loaded and the ACTION is what never ran. If neither appears, the DLL is not
// being loaded at all.
//
// It deliberately does not use GetResourcePath (which needs the API table to be valid): it goes to
// the fixed REAPER resource path as a fallback, and to the process's working directory otherwise.
static void WriteLoadMarker(const char *what)
{
  const char *cands[] = {
      "D:\\REAPER\\zoomgran_probe_loaded.txt",
      "C:\\Users\\EBo\\AppData\\Roaming\\REAPER\\zoomgran_probe_loaded.txt",
      "zoomgran_probe_loaded.txt",
  };
  for (int i = 0; i < 3; ++i)
  {
    FILE *f = fopen(cands[i], "ab");
    if (f)
    {
      fputs(what, f);
      fputc('\n', f);
      fclose(f);
      return;
    }
  }
}

extern "C" __declspec(dllexport) int ReaperPluginEntry(HINSTANCE hInst, reaper_plugin_info_t *rec)
{
  (void)hInst;
  WriteLoadMarker("entry point reached");
  if (!rec || rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc || !rec->Register)
  {
    WriteLoadMarker("entry point: BAD rec");
    return 0;
  }
  if (REAPERAPI_LoadAPI(rec->GetFunc) != 0)
  {
    WriteLoadMarker("entry point: REAPERAPI_LoadAPI FAILED");
    return 0; // a function this probe needs is missing; do not half-register
  }  WriteLoadMarker("entry point: API loaded");
  g_rec = rec;
  rec->Register("hookcommand2", (void *)HookCommand2);
  RegisterActions();
  {
    char b[128];
    _snprintf(b, sizeof(b), "actions registered: run=%d reset=%d state=%d", g_cmdRun, g_cmdReset,
              g_cmdState);
    WriteLoadMarker(b);
  }
  return 1;
}
