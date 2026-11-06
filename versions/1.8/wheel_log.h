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
  int horiz;       // 1 = this came from the HORIZONTAL wheel (WM_MOUSEHWHEEL), 0 = vertical
};

// ---------------------------------------------------------------------------
// WHAT WAS ACTUALLY SENT -- one record per call that reached REAPER.
//
// WHY THIS EXISTS (2026-10-09): a report said that with Slow step = 1 and a long Ramp-up, ONE notch of
// the smallest wheel increment makes three whole-unit receivers (main horizontal scroll, main vertical
// scroll, the piano roll's vertical scroll) "move, then bounce back". The delivery arithmetic was
// replayed offline over 16 parameter combinations and produced a POSITIVE net every time, with zero
// reverse sends -- so the arithmetic alone does not explain it. What the plugin can still be wrong
// about is what it hands over and WHEN, and that is exactly what this records: the signed units of
// every send, the encoded (val, valhw) REAPER actually receives, and the moment it happened relative
// to the wheel message and to the gesture ending.
//
// The wheel record above cannot answer this: it says what ARRIVED, never what was sent back out.
// ---------------------------------------------------------------------------
struct SendLogRec
{
  double t;        // seconds since the plugin loaded
  double gapMs;    // since the previous SEND (not the previous wheel message)
  int command;     // the action being driven
  int section;     // its KBD section
  double units;    // the signed amount handed to ReplayAction
  int val;         // what REAPER is actually called with ...
  int valhw;       // ... including the fractional part (see EncodeRel1)
  int topUp;       // 1 = this send came from the end-of-gesture top-up, 0 = from the glide
  double accum;    // the accumulator AFTER this send (its leftover fraction)
};

class WheelLog
{
public:
  static const int kMax = 50;   // messages held (the ring)
  static const int kSlots = 8;  // distinct magnitudes tracked per verdict
  static const int kKinds = 4;  // Device::kUnknown/kNotched/kFreeSpin/kTouchpad
  static const int kMaxSends = 64; // SEND records held (their own ring; see SendLogRec)

  // A longer gap than this ends a gesture, which is a good moment to write the file. Given in the
  // header (not just declared) so a class-static double needs no out-of-class definition.
  static constexpr double kGestureGapMs = 400.0;

  void Init(const char *path, const char *tmpPath)
  {
    _snprintf(path_, sizeof(path_), "%s", path ? path : "");
    _snprintf(tmp_, sizeof(tmp_), "%s", tmpPath ? tmpPath : "");
    n_ = head_ = 0;
    nSend_ = headSend_ = 0;
    lastT_ = 0.0;
    lastSendT_ = 0.0;
    haveLast_ = false;
    haveLastSend_ = false;
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

  // Add one SEND record. Separate from Record() on purpose: a wheel message and a send are different
  // events with different rates (one notch can produce dozens of sends), and mixing them in one ring
  // would push the wheel evidence out of the file. Its own gap is measured against the previous SEND,
  // which is what shows the delivery cadence.
  void RecordSend(const SendLogRec &r, double nowSec)
  {
    SendLogRec out = r;
    out.t = nowSec;
    out.gapMs = haveLastSend_ ? (nowSec - lastSendT_) * 1000.0 : 0.0;
    lastSendT_ = nowSec;
    haveLastSend_ = true;
    send_[headSend_] = out;
    headSend_ = (headSend_ + 1) % kMaxSends;
    if (nSend_ < kMaxSends)
      ++nSend_;
  }

  const SendLogRec &SendAt(int i) const
  {
    const int idx = (nSend_ < kMaxSends) ? i : ((headSend_ + i) % kMaxSends);
    return send_[idx];
  }
  int SendCount() const { return nSend_; }

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
    //
    // THE `axis` COLUMN tells the two wheels apart, and it is APPENDED rather than folded into an
    // existing column: a tilt wheel / thumb wheel arrives as WM_MOUSEHWHEEL and reaches REAPER as a
    // different set of actions, so a report that mixed the two silently would be unreadable exactly
    // when it is needed. `V` is the ordinary vertical wheel (every record written before this column
    // existed), `H` is the horizontal one.
    fputs("#\n#  n     t_ms   gap_ms   delta   key    extra-word  anim  device      axis  window\n", f);
    for (int i = 0; i < n_; ++i)
    {
      const WheelLogRec &r = At(i);
      fprintf(f, "  %3d  %8.0f  %8.2f  %+6d  0x%04X  0x%08lX  %4d  %-10s  %-4s  %s\n", i + 1,
              r.t * 1000.0, r.gapMs, r.delta, r.key, r.extra, r.anim,
              kNames[(r.dev >= 0 && r.dev < kKinds) ? r.dev : 0], r.horiz ? "H" : "V", r.wincls);
    }
    fprintf(f, "# end -- the most recent %d messages\n", n_);

    // WHAT WAS SENT BACK OUT. This is the half the wheel table above cannot show, and it is the half
    // that answers "the view moved and came back": a reversal in the `units` column, or a send whose
    // `val`/`valhw` disagree with its sign, is visible here and nowhere else.
    fprintf(f, "#\n# sends: every call that reached REAPER, in order (most recent %d)\n", nSend_);
    fputs("#  n     t_ms   gap_ms    section/command      units      val   valhw  topup  accum\n", f);
    for (int i = 0; i < nSend_; ++i)
    {
      const SendLogRec &s = SendAt(i);
      fprintf(f, "  %3d  %8.0f  %8.2f    %6d/%-6d  %+10.4f  %+5d  %+5d  %5d  %+.5f\n", i + 1,
              s.t * 1000.0, s.gapMs, s.section, s.command, s.units, s.val, s.valhw, s.topUp, s.accum);
    }
    if (nSend_ == 0)
      fputs("#  (none -- no travel was ever handed to REAPER in this window)\n", f);
    fprintf(f, "# end -- the most recent %d sends\n", nSend_);
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
  // The SEND ring (see SendLogRec): a separate ring and a separate "previous" stamp, because its rate
  // is unrelated to the wheel's.
  SendLogRec send_[kMaxSends];
  int nSend_ = 0;
  int headSend_ = 0;
  double lastSendT_ = 0.0;
  bool haveLastSend_ = false;
  int histCount_[kKinds] = {0};
  Mag histMag_[kKinds][kSlots];
  int histN_[kKinds] = {0};
  bool histOver_[kKinds] = {false};
  char path_[260] = {0};
  char tmp_[260] = {0};
  bool writable_ = false;
};

#endif // SWS_WHEEL_LOG_H
