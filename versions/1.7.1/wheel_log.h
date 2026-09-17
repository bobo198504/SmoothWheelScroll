#ifndef SWS_WHEEL_LOG_H
#define SWS_WHEEL_LOG_H

// ---------------------------------------------------------------------------
// DEV WHEEL LOG -- the bookkeeping half, with no REAPER in it.
//
// The plugin's DEV build (SWS_WHEEL_LOG / ./build.sh --wheel-log) records the last few
// WM_MOUSEWHEEL messages it saw, so a tester on hardware we do not have can send their real values
// back. This header owns the part worth getting right -- the RING, the per-verdict tally, and the
// text that goes in the file -- and the plugin owns the machine-specific part (where the file is,
// what the clock says, what REAPER's version is).
//
// Split this way because the ring's ORDER is the whole point of the file: `At(0)` must be the
// OLDEST held message and the last one the newest, or the report reads backwards and a tester's
// round trip is wasted. That ordering, and the file's shape, are exercised outside REAPER by
// _diag/wheel_log_probe.cpp, which includes THIS file rather than a copy of it (the project's rule
// for testable logic -- see routing.h / device.h).
//
// What a record holds is deliberately narrow: when the message arrived and how long after the
// previous one, the raw delta, the message's key state and extra-info word, the window CLASS under
// the cursor, the classifier's verdict, and whether it was animated. NO project paths, track names,
// media or REAPER preferences -- the file is meant to be handed to someone else.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstring>

// The atomic file replace needs MoveFileExA. That is the one Windows dependency in here, and it is
// deliberate: C's rename() REFUSES to overwrite an existing file on this toolchain (measured -- it
// returns -1 and leaves the old file), so writing straight over the live log is not an option and
// the temp-file-plus-rename has to go through the Win32 call that does allow it.
#include <windows.h>

struct WheelLogRec
{
  double t;        // seconds since the plugin loaded
  double gapMs;    // since the previous message (the rate -- what a free-spinner is judged on)
  int delta;       // the raw value out of the message
  unsigned key;    // the message's key state
  long extra;      // GetMessageExtraInfo(): where Windows puts its touch/pen marker
  char wincls[40]; // the window class under the cursor
  int dev;         // the classifier's verdict, as an int (Device)
  int anim;        // 1 = animated, 0 = left to REAPER
};

class WheelLog
{
public:
  static const int kMax = 50;   // messages held (the ring)
  static const int kSlots = 8;  // distinct magnitudes tracked per verdict
  static const int kKinds = 4;  // Device::kUnknown/kNotched/kFreeSpin/kTouchpad

  // A longer gap than this ends a gesture, which is a good moment to write the file. Given in the
  // header (not just declared) so a class-static double needs no out-of-class definition.
  static constexpr double kGestureGapMs = 400.0;

  void Init(const char *path, const char *tmpPath)
  {
    _snprintf(path_, sizeof(path_), "%s", path ? path : "");
    _snprintf(tmp_, sizeof(tmp_), "%s", tmpPath ? tmpPath : "");
    n_ = head_ = 0;
    lastT_ = 0.0;
    haveLast_ = false;
    for (int i = 0; i < kKinds; ++i)
    {
      histCount_[i] = 0;
      histN_[i] = 0;
      histOver_[i] = false;
    }
    // Can this folder be written at all? Asked ONCE here rather than discovered 50 times at flush
    // time. The likely answer is yes (the folder the user just dropped the DLL into), but a REAPER
    // installed under Program Files can refuse a non-elevated write, and then the file would simply
    // never appear -- which looks like "the plugin is broken" to a tester rather than "that folder
    // is read-only". Knowing it up front also stops a pointless write attempt per second.
    writable_ = false;
    if (path_[0])
    {
      FILE *probe = fopen(path_, "a");
      if (!probe)
        probe = fopen(tmp_, "a");
      if (probe)
      {
        writable_ = true;
        fclose(probe);
      }
    }
  }

  // Ready to log: a path was derived AND it can be written.
  bool Ready() const { return path_[0] != 0 && writable_; }
  // A path was derived but the folder refuses writes (see Init). The caller may report this; the
  // header itself has nowhere else to put it without writing outside the plugin's own folder.
  bool PathKnownButReadOnly() const { return path_[0] != 0 && !writable_; }
  int Count() const { return n_; }

  // 0 = oldest held, Count()-1 = newest. The ring's wrap is why this is a function and not an
  // index into the array.
  const WheelLogRec &At(int i) const
  {
    const int idx = (n_ < kMax) ? i : ((head_ + i) % kMax);
    return rec_[idx];
  }

  const char *Path() const { return path_; }

  // Add one message. `nowSec` is the caller's clock; the gap is measured here so the caller does
  // not have to remember the previous stamp. Returns true when this message ENDS a gesture (the
  // caller may use that to write the file).
  bool Record(const WheelLogRec &r, double nowSec)
  {
    WheelLogRec out = r;
    out.t = nowSec;
    // "Is there a previous message?" is a separate flag, NOT `lastT_ > 0`: the first record after
    // load can legitimately be at t == 0 (a wheel arriving the instant the plugin loads), and using
    // the timestamp as its own sentinel then swallowed the gap for the SECOND message too -- which
    // is the measurement a free-spinning wheel is judged on. (Caught by _diag/wheel_log_probe.cpp.)
    out.gapMs = haveLast_ ? (nowSec - lastT_) * 1000.0 : 0.0;
    lastT_ = nowSec;
    haveLast_ = true;
    if (out.wincls[sizeof(out.wincls) - 1] != 0)
      out.wincls[sizeof(out.wincls) - 1] = 0; // the caller's string may be longer than the field

    rec_[head_] = out;
    head_ = (head_ + 1) % kMax;
    if (n_ < kMax)
      ++n_;
    Tally(out.dev, out.delta);
    return out.gapMs > kGestureGapMs;
  }

  // Write the whole ring. `header` is the caller's already-formatted comment block (lines starting
  // with '#'); it is written verbatim above the table. A temp file plus a rename, so an interrupted
  // write cannot destroy the previous good file.
  void Flush(const char *header) const
  {
    if (!Ready())
      return;
    FILE *f = fopen(tmp_, "w");
    if (!f)
      return;
    if (header)
      fputs(header, f);

    fputs("# messages by what the classifier called them, and the distinct deltas seen:\n", f);
    static const char *kNames[kKinds] = {"unknown", "notched", "free-spin", "touchpad"};
    for (int d = 0; d < kKinds; ++d)
    {
      fprintf(f, "#   %-10s %5d messages", kNames[d], histCount_[d]);
      if (histN_[d] > 0)
      {
        fputs("  deltas:", f);
        for (int i = 0; i < histN_[d]; ++i)
          fprintf(f, " %d x%d", histMag_[d][i].mag, histMag_[d][i].count);
        if (histOver_[d])
          fputs(" (more)", f);
      }
      fputc('\n', f);
    }

    // The setting names are spelled out so a reader who did not build the plugin can still read
    // the file.
    fputs("#\n#  n     t_ms   gap_ms   delta   key    extra-word  anim  device      window\n", f);
    for (int i = 0; i < n_; ++i)
    {
      const WheelLogRec &r = At(i);
      fprintf(f, "  %3d  %8.0f  %8.2f  %+6d  0x%04X  0x%08lX  %4d  %-10s  %s\n", i + 1, r.t * 1000.0,
              r.gapMs, r.delta, r.key, r.extra, r.anim, kNames[(r.dev >= 0 && r.dev < kKinds) ? r.dev : 0],
              r.wincls);
    }
    fprintf(f, "# end -- the most recent %d messages\n", n_);
    fclose(f);
    MoveFileExA(tmp_, path_, MOVEFILE_REPLACE_EXISTING);
  }

private:
  struct Mag { int mag; int count; };

  void Tally(int dev, int delta)
  {
    const int d = (dev >= 0 && dev < kKinds) ? dev : 0;
    const int mag = (delta < 0) ? -delta : delta;
    ++histCount_[d];
    for (int i = 0; i < histN_[d]; ++i)
      if (histMag_[d][i].mag == mag)
      {
        ++histMag_[d][i].count;
        return;
      }
    if (histN_[d] < kSlots)
    {
      histMag_[d][histN_[d]].mag = mag;
      histMag_[d][histN_[d]].count = 1;
      ++histN_[d];
    }
    else
      histOver_[d] = true;
  }

  WheelLogRec rec_[kMax];
  int n_ = 0;
  int head_ = 0;
  double lastT_ = 0.0;
  bool haveLast_ = false;
  int histCount_[kKinds] = {0};
  Mag histMag_[kKinds][kSlots];
  int histN_[kKinds] = {0};
  bool histOver_[kKinds] = {false};
  char path_[260] = {0};
  char tmp_[260] = {0};
  bool writable_ = false;
};

#endif // SWS_WHEEL_LOG_H
