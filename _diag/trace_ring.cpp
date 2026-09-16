// Verify the trace's two rings by re-implementing their index logic and checking the ORDER shown.
// The panel shows the output ring "oldest first, newest last" and the eaten row "oldest .. newest";
// getting the modulo/shift wrong would silently show the wrong order or drop values.
#include <cstdio>
#include <cmath>

struct PanelTrace {
  static const int kIn = 6, kOut = 50, kEat = 6;
  int inDelta[kIn] = {0}; int inN = 0;
  double gapMs = 0.0, lastMsgMs = 0.0; bool seen = false;
  double outDelta[kOut] = {0}; long outCount = 0;
  double eatWin[kEat] = {0}; double eatSlot = 0.0; bool eatSeen = false;
};
static PanelTrace t;

static void TraceInput(int delta, double nowMs) {
  t.gapMs = t.seen ? (nowMs - t.lastMsgMs) : 0.0;
  t.lastMsgMs = nowMs; t.seen = true;
  for (int i = 0; i + 1 < PanelTrace::kIn; ++i) t.inDelta[i] = t.inDelta[i + 1];
  t.inDelta[PanelTrace::kIn - 1] = delta;
  if (t.inN < PanelTrace::kIn) ++t.inN;
}
static void TraceOutput(double d) { t.outDelta[t.outCount % PanelTrace::kOut] = d; ++t.outCount; }
static void TraceEaten(double d, double nowMs) {
  if (!(d > 0.0)) return;
  const double slot = nowMs * 0.1; // 100 units per second = 10 ms per unit
  if (!t.eatSeen) { t.eatSeen = true; t.eatSlot = slot; for (int i=0;i<PanelTrace::kEat;++i) t.eatWin[i]=0.0; }
  const double dd = slot - t.eatSlot;
  if (dd >= 1.0) {
    int shift = (int)dd; if (shift > PanelTrace::kEat) shift = PanelTrace::kEat;
    for (int i = 0; i + shift < PanelTrace::kEat; ++i) t.eatWin[i] = t.eatWin[i + shift];
    for (int i = PanelTrace::kEat - shift; i < PanelTrace::kEat; ++i) t.eatWin[i] = 0.0;
    t.eatSlot = slot;
  }
  t.eatWin[PanelTrace::kEat - 1] += d;
}

int main() {
  // --- output ring: push 60 values; display must show the LAST 50, oldest first (11..60) ---
  for (int v = 1; v <= 60; ++v) TraceOutput(v);
  const int count = (t.outCount < PanelTrace::kOut) ? (int)t.outCount : PanelTrace::kOut;
  const int first = (t.outCount < PanelTrace::kOut) ? 0 : (int)(t.outCount % PanelTrace::kOut);
  printf("output: outCount=%ld count=%d first-index=%d\n", t.outCount, count, first);
  printf("  shown: ");
  bool ok = true;
  for (int k = 0; k < count; ++k) {
    const double v = t.outDelta[(first + k) % PanelTrace::kOut];
    if (k < 6 || k >= count - 3) printf("%.0f ", v);
    else if (k == 6) printf("... ");
    if (v != 11.0 + k) ok = false;
  }
  printf("\n  expect 11 .. 60 in order  -> %s\n", ok ? "OK" : "WRONG");

  // --- input ring: 8 deltas; must show the last 6, oldest first ---
  const int vals[] = {10, 20, 30, 40, 50, 60, 70, 80};
  for (int i = 0; i < 8; ++i) TraceInput(vals[i], (double)(i * 100));
  printf("\ninput: inN=%d  shown: ", t.inN);
  bool iok = (t.inN == PanelTrace::kIn);
  const int exp[] = {30, 40, 50, 60, 70, 80};
  for (int i = PanelTrace::kIn - t.inN; i < PanelTrace::kIn; ++i) {
    printf("%d ", t.inDelta[i]);
    if (t.inDelta[i] != exp[i - (PanelTrace::kIn - t.inN)]) iok = false;
  }
  printf("\n  expect 30 40 50 60 70 80  -> %s\n", iok ? "OK" : "WRONG");
  printf("  gap between last two = %.1f ms (expect 100.0)\n", t.gapMs);

  // --- eaten windows: eat 3 deltas at t=0, then 5 more 40 ms later (4 slots on) ---
  // Slot k holds "eatWin[kEat-1-k]" ... so with the current slot at index 5:
  //   index 5 = now (slot 4), index 1 = slot 0. The 3 must sit at index 1, the 5 at index 5.
  t = PanelTrace();
  TraceEaten(3.0, 0.0);
  TraceEaten(5.0, 40.0);
  printf("\neaten (after 3 at t=0, 5 at t=40ms), oldest..newest: ");
  for (int i = 0; i < PanelTrace::kEat; ++i) printf("%.0f ", t.eatWin[i]);
  printf("\n  expect 0 3 0 0 0 5 (3 is 4 slots back, 5 is now)  -> %s\n",
         (t.eatWin[1] == 3 && t.eatWin[5] == 5 && t.eatWin[0] == 0) ? "OK" : "WRONG");

  // --- eaten windows: a big jump (>6 slots) must zero the whole row ---
  TraceEaten(1.0, 500.0); // 460 ms later => 46 slots
  printf("\neaten after a 460 ms pause then 1 delta: ");
  for (int i = 0; i < PanelTrace::kEat; ++i) printf("%.0f ", t.eatWin[i]);
  printf("\n  expect 0 0 0 0 0 1  -> %s\n",
         (t.eatWin[5] == 1 && t.eatWin[0] == 0) ? "OK" : "WRONG");
  return 0;
}
